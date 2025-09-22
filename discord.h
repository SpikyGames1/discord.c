// discord.h
#ifndef DISCORD_H
#define DISCORD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <libwebsockets.h>
#include <pthread.h>

#define MAX_COMMANDS 10
#define MAX_RESPONSE_SIZE 4096

typedef struct {
    char *name;
    char *description;
    char *response_message;
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
} discord_bot_t;

// Initialize the bot with a token
discord_bot_t* discord_init(const char *token);

// Clean up resources
void discord_cleanup(discord_bot_t *bot);

// Send a message to a channel
void discord_send_message(discord_bot_t *bot, const char *channel_id, const char *message);

// Add a slash command
int discord_add_slash_command(discord_bot_t *bot, const char *name, const char *description, const char *response);

// Register all slash commands with Discord
int discord_register_commands(discord_bot_t *bot);

// Start the bot (connects to gateway and listens for commands)
int discord_start_bot(discord_bot_t *bot);

// Get Gateway URL from Discord API
int discord_get_gateway_url(discord_bot_t *bot);

// Stop the bot
void discord_stop_bot(discord_bot_t *bot);

// Send interaction response
void discord_send_interaction_response(discord_bot_t *bot, const char *interaction_id, const char *interaction_token, const char *message);

// Get application ID from token (helper function)
int discord_get_application_id(discord_bot_t *bot);

#endif
