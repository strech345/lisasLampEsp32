// Web-Authentifizierung — Implementierung
// Tokens werden als 32-Hex-Zeichen-Strings gespeichert (16 zufällige Bytes via esp_random).
// Cookie-Parsing erfolgt manuell, da ESPAsyncWebServer keine dedizierte Cookie-API bietet.
#include "web_auth.h"
#include "store.h"
#include "debug_utils.h"
#include <esp_random.h>

static const SystemSettings* g_authSettings = nullptr;
static AuthTokenStore g_tokenStore;

void initWebAuth(const SystemSettings* settings) {
    g_authSettings = settings;
    if(!loadAuthTokens(g_tokenStore)) {
        serialPrint("WebAuth: keine gespeicherten Tokens gefunden");
    } else {
        serialPrint(String("WebAuth: ") + g_tokenStore.count + " Token(s) geladen");
    }
}

// Extrahiert den Wert des Cookies mit dem angegebenen Namen aus dem Cookie-Header
static String extractCookieValue(const String& cookieHeader, const char* name) {
    String search = String(name) + "=";
    int start = cookieHeader.indexOf(search);
    if(start == -1) return "";
    start += search.length();
    int end = cookieHeader.indexOf(';', start);
    if(end == -1) end = cookieHeader.length();
    return cookieHeader.substring(start, end);
}

bool isAuthorized(AsyncWebServerRequest* request) {
    // Kein Passwort → immer erlaubt
    if(!g_authSettings || g_authSettings->webPassword[0] == '\0') return true;

    String cookieHeader = request->header("Cookie");
    if(cookieHeader.isEmpty()) return false;

    String token = extractCookieValue(cookieHeader, "auth");
    if(token.isEmpty()) return false;

    // Token gegen gespeicherte Tokens prüfen
    for(uint8_t i = 0; i < g_tokenStore.count; i++) {
        if(token.equals(g_tokenStore.tokens[i])) return true;
    }
    return false;
}

// Erzeugt 16 zufällige Bytes über Hardware-RNG und kodiert sie als 32-Hex-Zeichen-String
String createAndStoreToken() {
    uint8_t raw[AUTH_TOKEN_LEN];
    for(int i = 0; i < AUTH_TOKEN_LEN; i += 4) {
        uint32_t r = esp_random();
        raw[i + 0] = (r >> 24) & 0xFF;
        raw[i + 1] = (r >> 16) & 0xFF;
        raw[i + 2] = (r >>  8) & 0xFF;
        raw[i + 3] = (r >>  0) & 0xFF;
    }

    char hex[AUTH_TOKEN_LEN * 2 + 1];
    for(int i = 0; i < AUTH_TOKEN_LEN; i++) {
        snprintf(hex + i * 2, 3, "%02x", raw[i]);
    }

    // FIFO: ältesten Slot überschreiben wenn voll
    uint8_t slot = g_tokenStore.nextSlot;
    strlcpy(g_tokenStore.tokens[slot], hex, sizeof(g_tokenStore.tokens[slot]));
    g_tokenStore.nextSlot = (slot + 1) % MAX_AUTH_TOKENS;
    if(g_tokenStore.count < MAX_AUTH_TOKENS) g_tokenStore.count++;

    saveAuthTokens(g_tokenStore);
    serialPrint(String("WebAuth: Token gespeichert (Slot ") + slot + ", gesamt " + g_tokenStore.count + ")");
    return String(hex);
}

uint8_t getTokenCount() {
    return g_tokenStore.count;
}
