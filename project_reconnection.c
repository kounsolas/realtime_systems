#include <libwebsockets.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#define GREEN_TEXT "\033[32m"
#define RESET_TEXT "\033[0m"

#define API_KEY "cqbugmhr01qmbcu8sr9gcqbugmhr01qmbcu8sra0"

// Global variable to store WebSocket instance
struct lws *wsi = NULL;  // Declare wsi globally

// Structures for candlestick and trade data
typedef struct {
    double open;
    double close;
    double high;
    double low;
    float volume;  // Volume as a float to account for fractional trades
    double total_trade_price;  // Sum of (trade price * volume)
    float total_trade_volume;  // Sum of volumes (float to handle fractional volumes)
    double time_difference;    // Time difference for candlestick data
} Candlestick;

typedef struct {
    double price;
    float volume;
    long long timestamp;
} TradeData;

typedef struct TradeNode {
    TradeData trade;
    struct TradeNode *next;
} TradeNode;

typedef struct {
    TradeNode *front;
    TradeNode *rear;
    pthread_mutex_t lock;
} TradeQueue;

typedef struct {
    Candlestick candlesticks[15]; // Store last 15 candlesticks for moving average
    int count;
    pthread_mutex_t lock;
} CandlestickList;

TradeQueue trade_queues[100];  // Dynamic queues for trades, one for each symbol
CandlestickList candlestick_list[100];  // Candlestick data for each symbol
int symbol_count = 0;

char *symbols[] = {"AAPL", "AMZN", "TSLA", "BINANCE:BTCUSDT"};
int num_symbols = sizeof(symbols) / sizeof(symbols[0]);

// Initialize the queue
void init_queue(TradeQueue *queue) {
    queue->front = queue->rear = NULL;
    pthread_mutex_init(&queue->lock, NULL);
}

// Enqueue trade data into the queue
void enqueue_trade(TradeQueue *queue, TradeData trade) {
    TradeNode *new_node = (TradeNode *)malloc(sizeof(TradeNode));
    new_node->trade = trade;
    new_node->next = NULL;
    
    pthread_mutex_lock(&queue->lock);
    if (queue->rear == NULL) {
        queue->front = queue->rear = new_node;
    } else {
        queue->rear->next = new_node;
        queue->rear = new_node;
    }
    pthread_mutex_unlock(&queue->lock);
}

// Dequeue trade data from the queue
int dequeue_trade(TradeQueue *queue, TradeData *trade) {
    pthread_mutex_lock(&queue->lock);
    if (queue->front == NULL) {
        pthread_mutex_unlock(&queue->lock);
        return 0;  // Queue is empty
    }

    TradeNode *temp = queue->front;
    *trade = temp->trade;
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    free(temp);
    pthread_mutex_unlock(&queue->lock);
    return 1;
}


// Function to write trade data to file
void write_trade_to_file(const char* symbol, const TradeData* trade) {
    FILE *file;
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_trades.txt", symbol);
    file = fopen(filename, "a");

    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file, "%-20s%-15s%-15s\n", "Timestamp", "Price", "Volume");
    }

    if (file) {
        fprintf(file, "%-20lld%-15.2f%-15f\n", trade->timestamp, trade->price, trade->volume);
        fclose(file);
    }
}

// Function to write candlestick data to file
void write_candlestick_to_file(const char* symbol, const Candlestick* candlestick, int is_same_as_last) {
    FILE *file;
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_candlesticks.txt", symbol);
    file = fopen(filename, "a");

    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file, "%-12s%-12s%-12s%-12s%-15s%-20s\n", "Open", "Close", "High", "Low", "Volume", "Time_Difference(ms)");
    }

    if (file) {
        if (is_same_as_last) {
            fprintf(file, "%-12.2f%-12.2f%-12.2f%-12.2f%-15f%-20.6f (same as previous minute)\n",
                    candlestick->open, candlestick->close,
                    candlestick->high, candlestick->low,
                    candlestick->volume, candlestick->time_difference);
        } else {
            fprintf(file, "%-12.2f%-12.2f%-12.2f%-12.2f%-15f%-20.6f\n",
                    candlestick->open, candlestick->close,
                    candlestick->high, candlestick->low,
                    candlestick->volume, candlestick->time_difference);
        }
        fclose(file);
    }
}

// Function to write moving average data to file
void write_moving_average_to_file(const char* symbol, double moving_avg, float total_volume, double time_difference) {
    FILE *file;
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_moving_avg.txt", symbol);
    file = fopen(filename, "a");

    // Write header if file is empty (first time writing to it)
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file, "%-20s%-15s%-20s\n", "Moving Average", "Total Volume", "Time_Difference(ms)");
    }

    if (file) {
        fprintf(file, "%-20.2f%-15f%-20.6f\n", moving_avg, total_volume, time_difference);  // Write float volume and time difference
        fclose(file);
    }
}

// Function to calculate the time difference between current time and the expected time (59.999999s)
double calculate_time_difference(struct timeval current_time) {
    long expected_time_s = 59;       // 59 seconds
    long expected_time_us = 999999;  // 999999 microseconds (ideal time to close the candlestick)
    
    long current_time_s = current_time.tv_sec % 60;
    long current_time_us = current_time.tv_usec;

    double difference_s = (double)(current_time_s - expected_time_s);
    double difference_us = (double)(current_time_us - expected_time_us) / 1000000.0;  // Convert microseconds to seconds

    return difference_s + difference_us;  // Return the time difference in seconds
}

// Process trades for each symbol and compute candlestick and moving average
void process_trades(const char* symbol, int i) {
    TradeQueue *queue = &trade_queues[i];
    CandlestickList *c_list = &candlestick_list[i];

    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    double time_diff = calculate_time_difference(current_time);  // Calculate the time difference

    pthread_mutex_lock(&c_list->lock);  // Locking for thread safety
    printf("Processing trades for symbol: %s at the end of the minute\n", symbol);

    Candlestick candle = {0};
    candle.time_difference = time_diff;
    int is_same_as_last = 0;
    TradeData trade;

    while (dequeue_trade(queue, &trade)) {  // Dequeue all trades from the queue
        if (candle.volume == 0) {
            candle.open = trade.price;
            candle.high = trade.price;
            candle.low = trade.price;
        }
        candle.close = trade.price;
        if (trade.price > candle.high) candle.high = trade.price;
        if (trade.price < candle.low) candle.low = trade.price;
        candle.volume += trade.volume;
        candle.total_trade_price += trade.price * trade.volume;
        candle.total_trade_volume += trade.volume;  // Accumulate float volume
    }

    if (c_list->count > 0 && candle.volume == 0) {
        is_same_as_last = 1;
        candle = c_list->candlesticks[c_list->count - 1];  // Carry forward the last candlestick
    }

    if (c_list->count < 15) {
        c_list->candlesticks[c_list->count++] = candle;
    } else {
        memmove(&c_list->candlesticks[0], &c_list->candlesticks[1], sizeof(Candlestick) * 14);
        c_list->candlesticks[14] = candle;
    }

    write_candlestick_to_file(symbol, &candle, is_same_as_last);

    // Compute moving average
    double total_sum = 0.0;
    float total_vol = 0.0;  // Float for total volume to account for fractional trades

    for (int j = 0; j < c_list->count; j++) {
        total_sum += c_list->candlesticks[j].total_trade_price;
        total_vol += c_list->candlesticks[j].total_trade_volume;
    }

    double moving_avg = (total_vol > 0) ? total_sum / total_vol : 0;
    write_moving_average_to_file(symbol, moving_avg, total_vol, time_diff);

    pthread_mutex_unlock(&c_list->lock);  // Unlock after processing
}

static int callback_example(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

// Protocols array
static struct lws_protocols protocols[] = {
    { "lws-minimal-client", callback_example, 0, 0 },
    { NULL, NULL, 0, 0 } // terminator
};

// WebSocket reconnection logic
void reconnect_websocket() {
    struct lws_client_connect_info i;
    memset(&i, 0, sizeof(i));

    i.context = lws_get_context(wsi);  // Use the global wsi
    i.address = "ws.finnhub.io";
    i.port = 443;
    i.path = "/?token=" API_KEY;
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;
    i.ssl_connection = LCCSCF_USE_SSL;
    i.pwsi = &wsi;

    printf("Attempting to reconnect to WebSocket...\n");

    if (lws_client_connect_via_info(&i)) {
        printf("Reconnected to WebSocket!\n");
    } else {
        printf("Reconnection failed. Retrying in 5 seconds...\n");
        sleep(5);
        reconnect_websocket();
    }
}

// Callback function for WebSocket events
static int callback_example(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf(GREEN_TEXT"Client connection established\n"RESET_TEXT);
            fflush(stdout);
            unsigned char msg[128];
            int len_msg;
            for (int i = 0; i < num_symbols; i++) {
                len_msg = snprintf((char *)msg, sizeof(msg), "{\"type\":\"subscribe\",\"symbol\":\"%s\"}", symbols[i]);
                lws_write(wsi, msg, len_msg, LWS_WRITE_TEXT);
                printf("Subscribed to symbol: %s\n", symbols[i]);
                fflush(stdout);
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf(GREEN_TEXT"\n\nReceived message:"RESET_TEXT" %.*s\n", (int)len, (char *)in);
            fflush(stdout);
            
            json_error_t error;
            json_t *root = json_loads((const char *)in, 0, &error);
            if (!root) {
                printf("Error: on line %d: %s\n", error.line, error.text);
                break;
            }

            json_t *data = json_object_get(root, "data");
            if (json_is_array(data)) {
                size_t index;
                json_t *value;
                json_array_foreach(data, index, value) {
                    json_t *price = json_object_get(value, "p");
                    json_t *symbol = json_object_get(value, "s");
                    json_t *volume = json_object_get(value, "v");
                    json_t *timestamp = json_object_get(value, "t");

                    if (json_is_number(price) && json_is_string(symbol) && json_is_number(volume) && json_is_number(timestamp)) {
                        const char *sym = json_string_value(symbol);
                        double pr = json_number_value(price);
                        float vol = (float)json_number_value(volume);
                        long long ts = (long long)json_number_value(timestamp);

                        TradeData trade = {pr, vol, ts};

                        for (int i = 0; i < num_symbols; i++) {
                            if (strcmp(sym, symbols[i]) == 0) {
                                enqueue_trade(&trade_queues[i], trade);
                                write_trade_to_file(sym, &trade);
                                break;
                            }
                        }
                    }
                }
            }

            json_decref(root);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf(GREEN_TEXT"Client connection error: %s\n"RESET_TEXT, in ? (char *)in : "(null)");
            fflush(stdout);
            reconnect_websocket();  // Reconnect on error
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            printf(GREEN_TEXT"Client connection closed\n"RESET_TEXT);
            fflush(stdout);
            reconnect_websocket();  // Reconnect when closed
            break;

        default:
            break;
    }
    return 0;
}



// Consumer thread for processing trades every minute
void* consumer(void *arg) {
    int i = *((int *)arg);
    free(arg);

    time_t last_process_time = time(NULL);  // Initialize the last process time for this thread
    printf("Started consumer thread for symbol: %s\n", symbols[i]);
    fflush(stdout);

    while (1) {
        time_t current_time = time(NULL);
        if (current_time - last_process_time >= 60) {
            process_trades(symbols[i], i);
            last_process_time = current_time;
        }
        //sleep(1);  // You can remove or keep this based on the system's behavior
    }
    return NULL;
}

int main() {
    struct lws_context_creation_info info;
    struct lws_client_connect_info i;
    static struct lws_context *context;

    memset(&info, 0, sizeof(info));
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    memset(&i, 0, sizeof(i));
    i.context = context;
    i.address = "ws.finnhub.io";
    i.port = 443;
    i.path = "/?token=" API_KEY;
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;
    i.ssl_connection = LCCSCF_USE_SSL;
    i.pwsi = &wsi;  // Use the global wsi

    lws_client_connect_via_info(&i);

    symbol_count = num_symbols;
    memset(candlestick_list, 0, sizeof(candlestick_list));

    // Initialize trade queues and candlestick list
    for (int i = 0; i < num_symbols; i++) {
        init_queue(&trade_queues[i]);
        pthread_mutex_init(&candlestick_list[i].lock, NULL);
    }

    // Create consumer threads for each symbol
    pthread_t consumers[num_symbols];
    for (int i = 0; i < num_symbols; i++) {
        int *arg = malloc(sizeof(*arg));
        *arg = i;
        pthread_create(&consumers[i], NULL, consumer, arg);
    }

    while (1) {
        lws_service(context, 1000);
        fflush(stdout);
    }

    lws_context_destroy(context);
    return 0;
}
