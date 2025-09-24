// discord.h
#ifndef DISCORD_H
#define DISCORD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <jansson.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#define MAX_COMMANDS 200
#define MAX_RESPONSE_SIZE 4096
#define MAX_EMBED_FIELDS 10

// Function pointer type for command handlers


// Embed structure
typedef struct {
    char *title;
    char *description;
    char *footer;
    unsigned int color; // Hex color code
    time_t timestamp;
} discord_embed_t;

// Message structure
typedef struct {
    char *content;
    bool ephemeral;
    discord_embed_t *embed; // Can be NULL if no embed
} discord_message_t;

typedef discord_message_t* (*command_handler_t)(void);

// Slash command structure
typedef struct {
    char *name;
    char *description;
    command_handler_t handler;
} slash_command_t;

typedef struct {
    char *data;
    size_t size;
} response_buffer_t;

typedef struct {
    char *token;
    char *gateway_url;
    char *application_id;
    CURL *curl;
    
    // Slash commands
    slash_command_t commands[MAX_COMMANDS];
    int command_count;
    
    // WebSocket related
    struct lws_context *ws_context;
    struct lws *ws_connection;
    pthread_t gateway_thread;
    int should_stop;
    
    // Latency tracking
    struct timeval last_heartbeat_sent;
    struct timeval last_heartbeat_ack;
    int heartbeat_acked;
    int heartbeat_interval;
    long gateway_latency_ms;
    pthread_mutex_t latency_mutex;
} discord_bot_t;

// Initialize the bot with a token
discord_bot_t* discord_init(const char *token);

// Clean up resources
void discord_cleanup(discord_bot_t *bot);

// Unified message sending function
void discord_send_message(discord_bot_t *bot, const char *channel_id, discord_message_t *message);

// Create and destroy message structures
discord_message_t* discord_create_message(const char *content, bool ephemeral);
void discord_destroy_message(discord_message_t *message);

// Embed management functions
discord_embed_t* discord_create_embed(const char *title, const char *description, unsigned int color);
void discord_destroy_embed(discord_embed_t *embed);
void discord_set_embed_footer(discord_embed_t *embed, const char *footer);
void discord_set_embed_timestamp(discord_embed_t *embed, time_t timestamp);
void discord_message_set_embed(discord_message_t *message, discord_embed_t *embed);

// Command management (separated from handling)
int discord_register_slash_command(discord_bot_t *bot, const char *name, const char *description, command_handler_t handler);
int discord_register_all_commands(discord_bot_t *bot);

// Start the bot (connects to gateway and listens for commands)
int discord_start_bot(discord_bot_t *bot);

// Get Gateway URL from Discord API
int discord_get_gateway_url(discord_bot_t *bot);

// Stop the bot
void discord_stop_bot(discord_bot_t *bot);

// Send interaction response (using new message structure)
void discord_send_interaction_response(discord_bot_t *bot, const char *interaction_id, const char *interaction_token, discord_message_t *message);

// Get application ID from token (helper function)
int discord_get_application_id(discord_bot_t *bot);

// Get current gateway latency in milliseconds
long discord_get_latency(discord_bot_t *bot);

void discord_set_global_bot(discord_bot_t *bot);

#endif
