#include "../discord.h"
#include <signal.h>
#include <unistd.h>

const char *bot_token = "YOUR_TOKEN_HERE";


// Global bot instance for signal handling
static discord_bot_t *g_bot = NULL;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    if (g_bot) {
        discord_stop_bot(g_bot);
    }
}

// Command handlers - these functions are called when slash commands are used
discord_message_t* ping_command(void) {
    // Get the global bot instance to check latency
    long latency = discord_get_latency(g_bot);
    
    char *response = malloc(256);
    if (latency >= 0) {
        snprintf(response, 256, "🏓 Pong! Gateway latency: %ldms", latency);
    } else {
        snprintf(response, 256, "🏓 Pong! (Latency unknown)");
    }
    return discord_create_message(response, false);
}

discord_message_t* hello_command(void) {
    char *response = malloc(256);
    snprintf(response, 256, "👋 Hello there! I'm a Discord bot written in C!");
    return discord_create_message(response, false);
}

discord_message_t* time_command(void) {
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    
    char *response = malloc(256);
    snprintf(response, 256, "🕐 Current server time: %s", asctime(local_time));
    
    // Remove trailing newline from asctime
    char *newline = strchr(response, '\n');
    if (newline) *newline = '\0';
    
    return discord_create_message(response, false);
}

discord_message_t* info_command(void) {
    char *response = malloc(512);
    snprintf(response, 512, 
        "ℹ️ **Bot Information**\n"
        "• Language: C\n"
        "• Library: Custom Discord C Library\n"
        "• Features: Slash Commands, Embeds, WebSocket Gateway\n"
        "• Status: Online and ready!");
    return discord_create_message(response, false);
}

discord_message_t* embed_demo_command(void) {
    // Create both content and embed in a single message
    discord_message_t *message = discord_create_message("", false);
    
    // Create and attach the embed
    discord_embed_t *embed = discord_create_embed(
        "Embed Demo", 
        "This is an example of a rich embed message sent along with regular text!", 
        0x00ff00  // Green color
    );
    
    // Set footer and timestamp
    discord_set_embed_footer(embed, "Powered by Discord C Library");
    discord_set_embed_timestamp(embed, time(NULL));
    
    discord_message_set_embed(message, embed);
    
    return message;
}

int main() {  
    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Create bot instance
    printf("Initializing bot...\n");
    g_bot = discord_init(bot_token);
    if (!g_bot) {
        printf("Failed to initialize bot\n");
        curl_global_cleanup();
        return 1;
    }
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Set global bot instance for commands that need it
    discord_set_global_bot(g_bot);
    
    // Register slash commands
    printf("Registering slash commands...\n");
    discord_register_slash_command(g_bot, "ping", "Check bot latency", ping_command);
    discord_register_slash_command(g_bot, "hello", "Say hello to the bot", hello_command);
    discord_register_slash_command(g_bot, "time", "Get current server time", time_command);
    discord_register_slash_command(g_bot, "info", "Get bot information", info_command);
    discord_register_slash_command(g_bot, "embed", "Demonstrate embed functionality", embed_demo_command);
    
    // Register commands with Discord API
    printf("Registering commands with Discord...\n");
    if (!discord_register_all_commands(g_bot)) {
        printf("Failed to register some commands with Discord\n");
    }
    
    // Start the bot (connects to gateway)
    printf("Starting bot...\n");
    if (!discord_start_bot(g_bot)) {
        printf("Failed to start bot\n");
        discord_cleanup(g_bot);
        curl_global_cleanup();
        return 1;
    }
    
    printf("Bot is now running! Press Ctrl+C to stop.\n");
    printf("Available commands:\n");
    printf("  /ping  - Check bot latency\n");
    printf("  /hello - Get a greeting\n");
    printf("  /time  - Get current server time\n");
    printf("  /info  - Get bot information\n");
    printf("  /embed - See an embed example\n");
    
    // Keep the main thread alive
    while (g_bot && !g_bot->should_stop) {
        sleep(1);
        
        // Display current latency every 30 seconds
        static int counter = 0;
        if (++counter >= 30) {
            long latency = discord_get_latency(g_bot);
            if (latency >= 0) {
                printf("Gateway latency: %ldms\n", latency);
            }
            counter = 0;
        }
    }
    
    printf("Cleaning up...\n");
    discord_cleanup(g_bot);
    curl_global_cleanup();
    
    printf("Bot stopped.\n");
    return 0;
}
