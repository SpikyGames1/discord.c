// discord.c - Implementation
#include "discord.h"

// Global bot instance for built-in commands
static discord_bot_t *global_bot_instance = NULL;

// Set global bot instance
void discord_set_global_bot(discord_bot_t *bot) {
    global_bot_instance = bot;
}

// Create a new message
discord_message_t* discord_create_message(const char *content, bool ephemeral) {
    discord_message_t *msg = malloc(sizeof(discord_message_t));
    if (!msg) return NULL;
    
    memset(msg, 0, sizeof(discord_message_t));
    msg->ephemeral = ephemeral;
    
    if (content) {
        msg->content = strdup(content);
    }
    
    return msg;
}

// Destroy a message and its embed
void discord_destroy_message(discord_message_t *message) {
    if (!message) return;
    
    free(message->content);
    if (message->embed) {
        discord_destroy_embed(message->embed);
    }
    free(message);
}

// Create a new embed
discord_embed_t* discord_create_embed(const char *title, const char *description, unsigned int color) {
    discord_embed_t *embed = malloc(sizeof(discord_embed_t));
    if (!embed) return NULL;
    
    memset(embed, 0, sizeof(discord_embed_t));
    embed->color = color;
    
    if (title) {
        embed->title = strdup(title);
    }
    if (description) {
        embed->description = strdup(description);
    }
    
    return embed;
}

// Destroy an embed
void discord_destroy_embed(discord_embed_t *embed) {
    if (!embed) return;
    
    free(embed->title);
    free(embed->description);
    free(embed->footer);
    free(embed);
}

// Set embed footer
void discord_set_embed_footer(discord_embed_t *embed, const char *footer) {
    if (!embed) return;
    
    free(embed->footer);
    if (footer) {
        embed->footer = strdup(footer);
    } else {
        embed->footer = NULL;
    }
}

// Set embed timestamp
void discord_set_embed_timestamp(discord_embed_t *embed, time_t timestamp) {
    if (!embed) return;
    
    embed->timestamp = timestamp;
}

// Attach embed to message
void discord_message_set_embed(discord_message_t *message, discord_embed_t *embed) {
    if (!message) return;
    
    if (message->embed) {
        discord_destroy_embed(message->embed);
    }
    message->embed = embed;
}

// Get current latency in milliseconds
long discord_get_latency(discord_bot_t *bot) {
    if (!bot) return -1;
    
    pthread_mutex_lock(&bot->latency_mutex);
    long latency = bot->gateway_latency_ms;
    pthread_mutex_unlock(&bot->latency_mutex);
    
    return latency;
}

// Calculate time difference in milliseconds
static long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_usec - start->tv_usec) / 1000;
}

// Send heartbeat
static void send_heartbeat(discord_bot_t *bot, struct lws *wsi) {
    json_t *heartbeat = json_object();
    json_object_set_new(heartbeat, "op", json_integer(1));
    json_object_set_new(heartbeat, "d", json_null());
    
    char *heartbeat_str = json_dumps(heartbeat, 0);
    if (heartbeat_str) {
        size_t msg_len = strlen(heartbeat_str);
        unsigned char *buf = malloc(LWS_PRE + msg_len);
        if (buf) {
            memcpy(&buf[LWS_PRE], heartbeat_str, msg_len);
            lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
            
            // Record heartbeat sent time
            pthread_mutex_lock(&bot->latency_mutex);
            gettimeofday(&bot->last_heartbeat_sent, NULL);
            bot->heartbeat_acked = 0;
            pthread_mutex_unlock(&bot->latency_mutex);
            
            free(buf);
        }
        free(heartbeat_str);
    }
    
    json_decref(heartbeat);
}

// Response buffer for HTTP requests
static size_t write_response_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buffer_t *buffer = (response_buffer_t *)userp;
    
    char *ptr = realloc(buffer->data, buffer->size + realsize + 1);
    if (!ptr) return 0;
    
    buffer->data = ptr;
    memcpy(&(buffer->data[buffer->size]), contents, realsize);
    buffer->size += realsize;
    buffer->data[buffer->size] = '\0';
    
    return realsize;
}

// Simple callback for requests that don't need response
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    return realsize;
}

// Build JSON payload from message structure
static char* build_message_payload(discord_message_t *message) {
    if (!message) return NULL;
    
    json_t *payload = json_object();
    
    // Add content if present
    if (message->content && strlen(message->content) > 0) {
        json_object_set_new(payload, "content", json_string(message->content));
    }
    
    // Add embed if present
    if (message->embed) {
        json_t *embed_obj = json_object();
        
        if (message->embed->title) {
            json_object_set_new(embed_obj, "title", json_string(message->embed->title));
        }
        if (message->embed->description) {
            json_object_set_new(embed_obj, "description", json_string(message->embed->description));
        }
        if (message->embed->footer) {
            json_t *footer_obj = json_object();
            json_object_set_new(footer_obj, "text", json_string(message->embed->footer));
            json_object_set_new(embed_obj, "footer", footer_obj);
        }
        if (message->embed->color != 0) {
            json_object_set_new(embed_obj, "color", json_integer(message->embed->color));
        }
        if (message->embed->timestamp != 0) {
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&message->embed->timestamp));
            json_object_set_new(embed_obj, "timestamp", json_string(timestamp));
        }
        
        json_t *embeds_array = json_array();
        json_array_append_new(embeds_array, embed_obj);
        json_object_set_new(payload, "embeds", embeds_array);
    }
    
    char *payload_str = json_dumps(payload, 0);
    json_decref(payload);
    
    return payload_str;
}

// Enhanced WebSocket callback with heartbeat and latency tracking
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    discord_bot_t *bot = (discord_bot_t *)lws_context_user(lws_get_context(wsi));
    static time_t last_heartbeat_time = 0;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connected to Discord Gateway\n");
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Allocate buffer with extra space for null terminator
            char *msg = malloc(len + 1);
            if (!msg) {
                printf("Failed to allocate memory for message\n");
                break;
            }
            
            memcpy(msg, in, len);
            msg[len] = '\0';
            
            // Parse the JSON message
            json_error_t error;
            json_t *root = json_loads(msg, 0, &error);
            if (!root) {
                printf("JSON parse error: %s\n", error.text);
                free(msg);
                break;
            }
            
            json_t *op = json_object_get(root, "op");
            json_t *t = json_object_get(root, "t");
            json_t *d = json_object_get(root, "d");
            
            if (!op) {
                json_decref(root);
                free(msg);
                break;
            }
            
            int opcode = json_integer_value(op);
            
            // Handle HELLO message (opcode 10)
            if (opcode == 10) {
                // Extract heartbeat interval
                if (d) {
                    json_t *heartbeat_interval_obj = json_object_get(d, "heartbeat_interval");
                    if (heartbeat_interval_obj) {
                        bot->heartbeat_interval = json_integer_value(heartbeat_interval_obj);
                    }
                }
                
                // Send IDENTIFY
                json_t *identify = json_object();
                json_object_set_new(identify, "op", json_integer(2));
                
                json_t *identify_data = json_object();
                json_object_set_new(identify_data, "token", json_string(bot->token));
                json_object_set_new(identify_data, "intents", json_integer(1 << 15)); // GUILD_MESSAGE_CONTENT
                
                json_t *properties = json_object();
                json_object_set_new(properties, "$os", json_string("linux"));
                json_object_set_new(properties, "$browser", json_string("discord_c_lib"));
                json_object_set_new(properties, "$device", json_string("discord_c_lib"));
                json_object_set_new(identify_data, "properties", properties);
                
                json_object_set_new(identify, "d", identify_data);
                
                char *identify_str = json_dumps(identify, 0);
                if (identify_str) {
                    size_t msg_len = strlen(identify_str);
                    unsigned char *buf = malloc(LWS_PRE + msg_len);
                    if (buf) {
                        memcpy(&buf[LWS_PRE], identify_str, msg_len);
                        lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
                        free(buf);
                    }
                    free(identify_str);
                }
                
                json_decref(identify);
            }
            // Handle HEARTBEAT_ACK (opcode 11)
            else if (opcode == 11) {
                pthread_mutex_lock(&bot->latency_mutex);
                gettimeofday(&bot->last_heartbeat_ack, NULL);
                bot->heartbeat_acked = 1;
                bot->gateway_latency_ms = timeval_diff_ms(&bot->last_heartbeat_sent, &bot->last_heartbeat_ack);
                pthread_mutex_unlock(&bot->latency_mutex);
            }
            // Handle INTERACTION_CREATE (slash commands)
            else if (t && strcmp(json_string_value(t), "INTERACTION_CREATE") == 0) {
              if (d) {
                json_t *interaction_type = json_object_get(d, "type");
        
                // Type 2 = Application Command
                if (interaction_type && json_integer_value(interaction_type) == 2) {
                  json_t *data_obj = json_object_get(d, "data");
                  json_t *command_name = json_object_get(data_obj, "name");
                  json_t *interaction_id = json_object_get(d, "id");
                  json_t *interaction_token = json_object_get(d, "token");
            
                  if (command_name && interaction_id && interaction_token) {
                    // Find matching command
                    const char *cmd_name = json_string_value(command_name);
                    for (int i = 0; i < bot->command_count; i++) {
                      if (strcmp(bot->commands[i].name, cmd_name) == 0) {
                        // Call the handler function which now returns discord_message_t*
                        discord_message_t *response_msg = bot->commands[i].handler();
                        
                        if (response_msg) {
                            discord_send_interaction_response(bot, 
                                json_string_value(interaction_id),
                                json_string_value(interaction_token),
                                response_msg);
                            
                            discord_destroy_message(response_msg);
                        }
                        break;
                    }
                }
            }
        }
    }
}
            
            json_decref(root);
            free(msg);
            break;
        }
        
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            // Send heartbeat if needed
            time_t current_time = time(NULL);
            if (bot->heartbeat_interval > 0 && 
                (current_time - last_heartbeat_time) >= (bot->heartbeat_interval / 1000)) {
                send_heartbeat(bot, wsi);
                last_heartbeat_time = current_time;
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection error\n");
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Connection closed\n");
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Gateway thread function with heartbeat timer
static void* gateway_thread_func(void *arg) {
    discord_bot_t *bot = (discord_bot_t *)arg;
    
    // Get the correct Gateway URL first
    if (!discord_get_gateway_url(bot)) {
        printf("Warning: Using fallback Gateway URL\n");
        // Fallback to hardcoded URL if API call fails
        if (bot->gateway_url) {
            free(bot->gateway_url);
        }
        bot->gateway_url = strdup("wss://gateway.discord.gg/?v=10&encoding=json");
    }
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    static struct lws_protocols protocols[] = {
        {
            "discord-gateway",
            ws_callback,
            0,
            MAX_RESPONSE_SIZE,
        },
        { NULL, NULL, 0, 0 }
    };
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = bot;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    bot->ws_context = lws_create_context(&info);
    if (!bot->ws_context) {
        printf("Failed to create WebSocket context\n");
        return NULL;
    }
    
    // Fixed URL parsing - allocate separate buffers
    char host[256] = "gateway.discord.gg";
    char path[256] = "/?v=10&encoding=json";
    int port = 443;
    
    // Parse the gateway URL properly
    if (bot->gateway_url && strncmp(bot->gateway_url, "wss://", 6) == 0) {
        const char *url_start = bot->gateway_url + 6; // Skip "wss://"
        const char *path_start = strchr(url_start, '/');
        
        if (path_start) {
            // Copy host part
            size_t host_len = path_start - url_start;
            if (host_len < sizeof(host)) {
                strncpy(host, url_start, host_len);
                host[host_len] = '\0';
            }
            
            // Copy path part
            strncpy(path, path_start, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        } else {
            // No path found, copy entire remaining part as host
            strncpy(host, url_start, sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';
        }
        
        // Check for port in host
        char *port_start = strchr(host, ':');
        if (port_start) {
            *port_start = '\0';
            port = atoi(port_start + 1);
        }
    }
    
    // Add query parameters if not present
    if (strstr(path, "v=10") == NULL) {
        strncat(path, "?v=10&encoding=json", sizeof(path) - strlen(path) - 1);
    }
    
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = bot->ws_context;
    ccinfo.address = host;
    ccinfo.port = port;
    ccinfo.path = path;
    ccinfo.host = host;
    ccinfo.origin = "origin";
    ccinfo.protocol = "discord-gateway";
    ccinfo.ssl_connection = LCCSCF_USE_SSL;
    
    printf("Connecting to: %s:%d%s\n", host, port, path);
    
    bot->ws_connection = lws_client_connect_via_info(&ccinfo);
    if (!bot->ws_connection) {
        printf("Failed to connect to Discord Gateway\n");
        lws_context_destroy(bot->ws_context);
        return NULL;
    }
    
    while (!bot->should_stop) {
        lws_service(bot->ws_context, 100);
        lws_callback_on_writable(bot->ws_connection);
    }
    
    lws_context_destroy(bot->ws_context);
    return NULL;
}

// Get application ID from Discord API
int discord_get_application_id(discord_bot_t *bot) {
    char url[] = "https://discord.com/api/v10/applications/@me";
    
    response_buffer_t response = {0};
    
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", bot->token);
    headers = curl_slist_append(headers, auth_header);
    
    curl_easy_setopt(bot->curl, CURLOPT_URL, url);
    curl_easy_setopt(bot->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(bot->curl);
    
    curl_slist_free_all(headers);
    curl_easy_reset(bot->curl);
    
    if (res == CURLE_OK && response.data) {
        json_t *root = json_loads(response.data, 0, NULL);
        if (root) {
            json_t *id = json_object_get(root, "id");
            if (id) {
                bot->application_id = strdup(json_string_value(id));
                json_decref(root);
                free(response.data);
                return 1;
            }
            json_decref(root);
        }
        free(response.data);
    }
    
    return 0;
}

// Initialize the bot
discord_bot_t* discord_init(const char *token) {
    discord_bot_t *bot = malloc(sizeof(discord_bot_t));
    if (!bot) return NULL;
    
    memset(bot, 0, sizeof(discord_bot_t));
    
    bot->token = strdup(token);
    bot->curl = curl_easy_init();
    bot->gateway_url = strdup("wss://gateway.discord.gg/?v=10&encoding=json");
    bot->gateway_latency_ms = -1; // Initialize to -1 (unknown)
    
    // Initialize mutex
    if (pthread_mutex_init(&bot->latency_mutex, NULL) != 0) {
        discord_cleanup(bot);
        return NULL;
    }
    
    if (!bot->token || !bot->curl || !bot->gateway_url) {
        discord_cleanup(bot);
        return NULL;
    }
    
    // Get application ID
    if (!discord_get_application_id(bot)) {
        printf("Warning: Failed to get application ID\n");
    }
    
    return bot;
}

// Get Gateway URL from Discord API
int discord_get_gateway_url(discord_bot_t *bot) {
    const char *url = "https://discord.com/api/v10/gateway/bot";
    
    response_buffer_t response = {0};
    
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", bot->token);
    headers = curl_slist_append(headers, auth_header);
    
    curl_easy_setopt(bot->curl, CURLOPT_URL, url);
    curl_easy_setopt(bot->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(bot->curl);
    
    curl_slist_free_all(headers);
    curl_easy_reset(bot->curl);
    
    if (res == CURLE_OK && response.data) {
        json_t *root = json_loads(response.data, 0, NULL);
        if (root) {
            json_t *url_obj = json_object_get(root, "url");
            if (url_obj) {
                // Free old gateway URL and set new one
                if (bot->gateway_url) {
                    free(bot->gateway_url);
                }
                bot->gateway_url = strdup(json_string_value(url_obj));
                
                printf("Got Gateway URL: %s\n", bot->gateway_url);
                
                json_decref(root);
                if (response.data) {
                    free(response.data);
                }
                return 1;
            }
            json_decref(root);
        }
        if (response.data) {
            free(response.data);
        }
    }
    
    printf("Failed to get Gateway URL, using fallback\n");
    return 0;
}

// Clean up resources
void discord_cleanup(discord_bot_t *bot) {
    if (bot) {
        discord_stop_bot(bot);
        
        free(bot->token);
        free(bot->gateway_url);
        free(bot->application_id);
        
        // Clean up commands
        for (int i = 0; i < bot->command_count; i++) {
            free(bot->commands[i].name);
            free(bot->commands[i].description);
            // Note: handler is a function pointer, no need to free
        }
        
        // Destroy mutex
        pthread_mutex_destroy(&bot->latency_mutex);
        
        if (bot->curl) {
            curl_easy_cleanup(bot->curl);
        }
        free(bot);
    }
}

// Unified message sending function
void discord_send_message(discord_bot_t *bot, const char *channel_id, discord_message_t *message) {
    if (!bot || !channel_id || !message) return;
    
    char url[256];
    snprintf(url, sizeof(url), "https://discord.com/api/v10/channels/%s/messages", channel_id);

    char *payload_str = build_message_payload(message);
    if (!payload_str) return;

    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", bot->token);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(bot->curl, CURLOPT_URL, url);
    curl_easy_setopt(bot->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(bot->curl, CURLOPT_POSTFIELDS, payload_str);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(bot->curl, CURLOPT_POST, 1L);

    CURLcode res = curl_easy_perform(bot->curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Request failed: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    free(payload_str);
    curl_easy_reset(bot->curl);
}

// Register a slash command (separated from handling)
int discord_register_slash_command(discord_bot_t *bot, const char *name, const char *description, command_handler_t handler) {
    if (!bot || !name || !description || !handler || bot->command_count >= MAX_COMMANDS) {
        return 0;
    }
    
    bot->commands[bot->command_count].name = strdup(name);
    bot->commands[bot->command_count].description = strdup(description);
    bot->commands[bot->command_count].handler = handler;
    bot->command_count++;
    
    return 1;
}

// Register all commands with Discord API
int discord_register_all_commands(discord_bot_t *bot) {
    if (!bot || !bot->application_id) return 0;
    
    for (int i = 0; i < bot->command_count; i++) {
        char url[256];
        snprintf(url, sizeof(url), "https://discord.com/api/v10/applications/%s/commands", bot->application_id);
        
        json_t *command = json_object();
        json_object_set_new(command, "name", json_string(bot->commands[i].name));
        json_object_set_new(command, "description", json_string(bot->commands[i].description));
        json_object_set_new(command, "type", json_integer(1)); // CHAT_INPUT
        
        char *command_str = json_dumps(command, 0);
        json_decref(command);
        
        if (!command_str) continue;
        
        struct curl_slist *headers = NULL;
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", bot->token);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_header);
        
        curl_easy_setopt(bot->curl, CURLOPT_URL, url);
        curl_easy_setopt(bot->curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(bot->curl, CURLOPT_POSTFIELDS, command_str);
        curl_easy_setopt(bot->curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(bot->curl, CURLOPT_POST, 1L);
        
        CURLcode res = curl_easy_perform(bot->curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Failed to register command %s: %s\n", bot->commands[i].name, curl_easy_strerror(res));
        } else {
            printf("Registered command: %s\n", bot->commands[i].name);
        }
        
        curl_slist_free_all(headers);
        free(command_str);
        curl_easy_reset(bot->curl);
    }
    
    return 1;
}

// Send interaction response using new message structure
void discord_send_interaction_response(discord_bot_t *bot, const char *interaction_id, const char *interaction_token, discord_message_t *message) {
    if (!bot || !interaction_id || !interaction_token || !message) return;
    
    char url[512];
    snprintf(url, sizeof(url), "https://discord.com/api/v10/interactions/%s/%s/callback", interaction_id, interaction_token);
    
    json_t *data = json_object();
    
    // Add content if present
    if (message->content && strlen(message->content) > 0) {
        json_object_set_new(data, "content", json_string(message->content));
    }
    
    // Add embed if present
    if (message->embed) {
        json_t *embed_obj = json_object();
        
        if (message->embed->title) {
            json_object_set_new(embed_obj, "title", json_string(message->embed->title));
        }
        if (message->embed->description) {
            json_object_set_new(embed_obj, "description", json_string(message->embed->description));
        }
        if (message->embed->footer) {
            json_t *footer_obj = json_object();
            json_object_set_new(footer_obj, "text", json_string(message->embed->footer));
            json_object_set_new(embed_obj, "footer", footer_obj);
        }
        if (message->embed->color != 0) {
            json_object_set_new(embed_obj, "color", json_integer(message->embed->color));
        }
        if (message->embed->timestamp != 0) {
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&message->embed->timestamp));
            json_object_set_new(embed_obj, "timestamp", json_string(timestamp));
        }
        
        json_t *embeds_array = json_array();
        json_array_append_new(embeds_array, embed_obj);
        json_object_set_new(data, "embeds", embeds_array);
    }
    
    // Add flags for ephemeral messages
    if (message->ephemeral) {
        json_object_set_new(data, "flags", json_integer(64)); // EPHEMERAL flag
    }
    
    json_t *response = json_object();
    json_object_set_new(response, "type", json_integer(4)); // CHANNEL_MESSAGE_WITH_SOURCE
    json_object_set_new(response, "data", data);
    
    char *response_str = json_dumps(response, 0);
    json_decref(response);
    
    if (!response_str) return;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(bot->curl, CURLOPT_URL, url);
    curl_easy_setopt(bot->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(bot->curl, CURLOPT_POSTFIELDS, response_str);
    curl_easy_setopt(bot->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(bot->curl, CURLOPT_POST, 1L);
    
    CURLcode res = curl_easy_perform(bot->curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send interaction response: %s\n", curl_easy_strerror(res));
    }
    
    curl_slist_free_all(headers);
    free(response_str);
    curl_easy_reset(bot->curl);
}

// Start the bot
int discord_start_bot(discord_bot_t *bot) {
    if (!bot) return 0;
    
    bot->should_stop = 0;
    
    if (pthread_create(&bot->gateway_thread, NULL, gateway_thread_func, bot) != 0) {
        return 0;
    }
    
    return 1;
}

// Stop the bot
void discord_stop_bot(discord_bot_t *bot) {
    if (!bot) return;
    
    bot->should_stop = 1;
    
    if (bot->gateway_thread) {
        pthread_join(bot->gateway_thread, NULL);
    }
}
