#include "../discord.h"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

discord_bot_t *global_bot = NULL;

void signal_handler(int sig) {
    if (global_bot) {
        printf("\nShutting down bot...\n");
        discord_stop_bot(global_bot);
        discord_cleanup(global_bot);
        exit(0);
    }
}

// Command handler functions - these will be called when the command is used
char* hello_command() {
    printf("Command /hello used\n");
    return strdup("Hello there!");
}

// Custom ping command that returns ONLY the latency number
char* ping_command() {
    printf("Command /ping used\n");

    long latency = discord_get_latency(global_bot);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "My latency is: %ld ms.", latency);

    return strdup(buffer); // Return a dynamically allocated copy
}

char* info_command() {
    printf("Command /info used\n");
    return strdup("This is a C Discord bot with latency tracking!");
}

int main() {
    printf("Starting Discord bot with latency tracking...\n");
    
    // Initialize bot
    discord_bot_t *bot = discord_init("YOUR_BOT_TOKEN");
    if (!bot) {
        fprintf(stderr, "Failed to initialize bot\n");
        return 1;
    }
    
    global_bot = bot;
    signal(SIGINT, signal_handler);
    
    // Set global bot for built-in commands (optional)
    discord_set_global_bot(bot);
    
    // Add slash commands with function pointers
    printf("Adding slash commands...\n");
    discord_add_slash_command(bot, "hello", "Say hello!", hello_command);
    discord_add_slash_command(bot, "ping", "Check bot latency in milliseconds", ping_command);
    discord_add_slash_command(bot, "info", "Get bot information", info_command);
    
    // Register commands with Discord
    printf("Registering commands with Discord...\n");
    if (!discord_register_commands(bot)) {
        fprintf(stderr, "Failed to register commands\n");
        discord_cleanup(bot);
        return 1;
    }
    
    // Start the bot (connects to Gateway and listens for slash commands)
    printf("Starting bot and connecting to Gateway...\n");
    if (!discord_start_bot(bot)) {
        fprintf(stderr, "Failed to start bot\n");
        discord_cleanup(bot);
        return 1;
    }
    
    printf("Bot is running! Use Ctrl+C to stop.\n");
    printf("Try using the slash commands: /hello, /ping, /info\n");
    printf("The /ping command will return the gateway latency in milliseconds.\n");
    
    // Keep the main thread alive
    while (1) {
        sleep(1);
    }
    
    return 0;
}
