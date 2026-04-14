/*
 * Shower Music Controller (Dynamic Two-Way Setup)
 * Display: 3.2" ILI9341 TFT, 320x240, Landscape
 * Board:   ESP32 (WROOM-32D)
 
 *
 * Buttons:
 * GPIO 26 = NEXT       (move down)
 * GPIO 27 = SELECT     (Short Press: Confirm | Long Press: Go Back)
 * GPIO 23 = SKIP       (sends BLE command to skip track)
 * GPIO 22 = UP         (move up)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── BLE UUIDs ──────────────────
#define SERVICE_UUID  "cb927983-3d04-4727-b54b-59d894bd4474"
#define CHAR_UUID     "db4fcbed-5dbd-48c2-8fb2-5eec8e0ed26c"

// ── Button pins ───────────────────────────────────────────────────────────
#define BTN_DOWN       26   // scroll down
#define BTN_SELECT     27   // select
#define BTN_SKIP       23   // skip song
#define BTN_UP         22   // scroll up
// #define BTN_PLAY       4   // future pins
// #define BTN_PAUSE      16   

// ── TFT ───────────────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

// ── BLE globals ───────────────────────────────────────────────────────────
BLEServer*         pServer         = nullptr;   // ble server
BLECharacteristic* pCharacteristic = nullptr;   // channel used to send/receive data
bool               bleConnected    = false;     // if phone is conencted or not
bool               redrawRequired  = false;     // if page need to redraw screen

// ── Colors ───────────────────────────────────────────────────────────────
#define COL_BG          0x1082
#define COL_HEADER_BG   0x2124
#define COL_HEADER_LINE 0x4A69
#define COL_ITEM_BG     0x2965
#define COL_ITEM_SEL    0x34BF
#define COL_TEXT        0xFFFF
#define COL_SUBTEXT     0xAD55
#define COL_ARROW       0xAD55
#define COL_BATT_FULL   0x07E0
#define COL_BATT_LOW    0xF800
#define COL_BT_ON       0x001F
#define COL_BT_OFF      0x4A69
#define COL_PROGRESS_BG 0x4228
#define COL_PROGRESS_FG 0x07FF
#define COL_BACK_BTN    0xBDF7

// ── Layout constants ──────────────────────────────────────────────────────
#define SCREEN_W        320
#define SCREEN_H        240
#define HEADER_H        42
#define ITEM_H          52
#define ITEM_MARGIN     8
#define ITEM_RADIUS     8
#define ITEM_X          12
#define ITEM_W          (SCREEN_W - 24)
#define ITEMS_Y_START   (HEADER_H + 10)

// ── App state ─────────────────────────────────────────────────────────────
enum AppScreen {
  SCREEN_MENU,
  SCREEN_NOW_PLAYING,
  SCREEN_PLAYLISTS,
  SCREEN_SONG_LIST
};

AppScreen currentScreen = SCREEN_MENU;

// ── Live Dynamic Data (Sent via Bluetooth) ────────────────────────────────
String livePlaylists[5];      // stores data in the array (5 at a time)
int    livePlaylistCount = 0;

String liveTracks[5];
int    liveTrackCount = 0;

// ── Selection & Pagination State ──────────────────────────────────────────
int  selectedItem        = 0;
int  playlistSelected    = 0;   // Global absolute index across all pages
int  globalTrackIndex    = 0;   // Global absolute index across all pages
int  currentTrackPage    = 0;   // Start index of current track page (0, 5, 10...)
int  currentPlaylistPage = 0;   // Start index of current playlist page (0, 5, 10...)
bool isLoadingTracks     = false;
int  batteryPct          = 78;

// ── Live App Data ─────────────────────────────────────────────────────────
String currentSong   = "Waiting for Phone...";
String currentArtist = "";
int currentVolume    = 72; 

// Time Tracking Variables for Live Progress Bar
int currentProgressSec = 0;
int totalDurationSec   = 1; 
bool isPlaying         = false;
unsigned long lastTickTime = 0;

const char* menuLabels[] = {
  "Now Playing",
  "Playlists"
};

// ── Forward declarations ──────────────────────────────────────────────────
void handleNextButton();
void handleSelectButton();
void handleUpButton();
void handleSkipButton();
void sendBleCommand(const char* cmd);
void drawCurrentScreen();
void drawMenuScreen();
void drawNowPlayingScreen();
void drawPlaylistsScreen();
void drawSongListScreen();
void drawHeader(const char* title);
void drawMenuItem(int index, bool selected);
void drawBattery(int x, int y, int pct);
void drawBluetoothIcon(int cx, int cy, bool connected);
void drawChevron(int x, int y, uint16_t colour);
void drawMusicNote(int x, int y, uint16_t colour);
void drawListIcon(int x, int y, uint16_t colour);
void drawBackButton(int x, int y, int w, int h, bool selected);
void drawProgressBar(int x, int y, int w, int h, int percent);

// ── BLE Connection Callbacks ──────────────────────────────────────────────
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConnected = true;
    redrawRequired = true;
    Serial.println("BLE: App connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    bleConnected = false;
    redrawRequired = true;
    livePlaylistCount = 0;
    Serial.println("BLE: App disconnected — restarting advertising");
    BLEDevice::startAdvertising();
  }
};

// ── Incoming BLE Data Handler ─────────────────────────────────────────────
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String received = String(pChar->getValue().c_str());

    if (received.length() > 0) {
      Serial.print("Received from App: ");
      Serial.println(received);

      if (received.startsWith("P:")) {
        received.remove(0, 2);
        livePlaylistCount = 0;
        int startIndex    = 0;
        int delimiterIndex = received.indexOf('|');


        while (delimiterIndex >= 0 && livePlaylistCount < 5) {
          livePlaylists[livePlaylistCount++] = received.substring(startIndex, delimiterIndex);
          startIndex     = delimiterIndex + 1;
          delimiterIndex = received.indexOf('|', startIndex);
        }
        if (startIndex < received.length() && livePlaylistCount < 5) {
          livePlaylists[livePlaylistCount++] = received.substring(startIndex);
        }
        isLoadingTracks = false;
        redrawRequired  = true;
      }
      else if (received.startsWith("N:")) {
        received.remove(0, 2); 
        
        int p1 = received.indexOf('|');
        int p2 = received.indexOf('|', p1 + 1);
        int p3 = received.indexOf('|', p2 + 1);
        
        if (p1 > 0 && p2 > 0 && p3 > 0) {
          currentSong   = received.substring(0, p1);
          currentArtist = received.substring(p1 + 1, p2);
          currentProgressSec = received.substring(p2 + 1, p3).toInt();
          totalDurationSec   = received.substring(p3 + 1).toInt();
          
          isPlaying = true;
          lastTickTime = millis(); // Start the stopwatch!
        } else if (p1 > 0) {
          // Fallback just in case it's only Song|Artist
          currentSong   = received.substring(0, p1);
          currentArtist = received.substring(p1 + 1);
        }
        
        redrawRequired = true; // Tell the screen to update
      }

      else if (received.startsWith("T:")) {
        received.remove(0, 2);
        liveTrackCount = 0;
        int startIndex = 0;
        int delimiterIndex = received.indexOf('|');

        while (delimiterIndex >= 0 && liveTrackCount < 5) {
          liveTracks[liveTrackCount++] = received.substring(startIndex, delimiterIndex);
          startIndex     = delimiterIndex + 1;
          delimiterIndex = received.indexOf('|', startIndex);
        }
        if (startIndex < received.length() && liveTrackCount < 5) {
          liveTracks[liveTrackCount++] = received.substring(startIndex);
        }
        isLoadingTracks = false;
        redrawRequired  = true;
      }
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BTN_DOWN,       INPUT_PULLUP);
  pinMode(BTN_SELECT,     INPUT_PULLUP);
  pinMode(BTN_SKIP, INPUT_PULLUP);
  pinMode(BTN_UP,         INPUT_PULLUP);
  // pinMode(BTN_PLAY,   INPUT_PULLUP);
  // pinMode(BTN_PAUSE,  INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  drawCurrentScreen();

  BLEDevice::init("ShowerController");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHAR_UUID,
    BLECharacteristic::PROPERTY_READ  |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as 'ShowerController'");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastPress = 0;

  if (redrawRequired) {
    drawCurrentScreen();
    redrawRequired = false;
  }

  // --- Live Progress Bar Ticker ---
  if (isPlaying && currentScreen == SCREEN_NOW_PLAYING) {
    if (millis() - lastTickTime >= 1000) {
      lastTickTime = millis(); // Reset stopwatch
      
      if (currentProgressSec < totalDurationSec) {
        currentProgressSec++; // Tick up 1 second
        
        // Calculate new percentage and redraw JUST the bar
        int percent = (currentProgressSec * 100) / totalDurationSec;
        drawProgressBar(110, 208, 170, 10, percent);
      }
      // --- NEW: AUTO-ADVANCE TRIGGER ---
      else if (currentProgressSec == totalDurationSec) {
        currentProgressSec++; // Add 1 more second so this block doesn't spam the phone

        // Ask the phone to check what the next song is
        sendBleCommand("REFRESH_NOW_PLAYING");
      }
    }
  }

  if (millis() - lastPress > 220) {

    if (digitalRead(BTN_DOWN) == LOW) {
      handleNextButton();
      lastPress = millis();
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      unsigned long pressStart = millis();
      while (digitalRead(BTN_SELECT) == LOW) { delay(10); }

      if (millis() - pressStart > 600) {
        // Long press = go back
        if (currentScreen == SCREEN_SONG_LIST) {
          currentScreen = SCREEN_PLAYLISTS;
          redrawRequired = true;
        } else if (currentScreen == SCREEN_PLAYLISTS || currentScreen == SCREEN_NOW_PLAYING) {
          currentScreen = SCREEN_MENU;
          redrawRequired = true;
        }
      } else {
        handleSelectButton();
      }
      lastPress = millis();
    }

    if (digitalRead(BTN_SKIP) == LOW) {
      handleSkipButton();
      lastPress = millis();
    }

    if (digitalRead(BTN_UP) == LOW) {
      handleUpButton();
      lastPress = millis();
    }
    // if (digitalRead(BTN_PLAY) == LOW) {
    //   handlePlayButton();
    //   lastPress = millis();
    // }

    // if (digitalRead(BTN_PAUSE) == LOW) {
    //   handlePauseButton();
    //   lastPress = millis();
    // }
    
    // (Make sure to update your skip button to use your new BTN_SKIP name!)
    if (digitalRead(BTN_SKIP) == LOW) {
      handleSkipButton();
      lastPress = millis();
    }
  }
}

// ── Skip button ───────────────────────────────────────────────────────────
void handleSkipButton() {
  sendBleCommand("NEXT");
  Serial.println("Button pressed → sending: NEXT");
}

// // ── Play button ───────────────────────────────────────────────────────────
// void handlePlayButton() {
//   sendBleCommand("PLAY");
//   Serial.println("Button pressed → sending: PLAY");
  
//   // If we are looking at the Now Playing screen, resume the local progress bar
//   if (currentScreen == SCREEN_NOW_PLAYING) {
//     isPlaying = true;
//     lastTickTime = millis(); // Reset the stopwatch so it doesn't instantly jump
//   }
// }

// // ── Pause button ──────────────────────────────────────────────────────────
// void handlePauseButton() {
//   sendBleCommand("PAUSE");
//   Serial.println("Button pressed → sending: PAUSE");
  
//   // Instantly freeze the local progress bar timer!
//   isPlaying = false; 
// }

// ── Send BLE command ──────────────────────────────────────────────────────
void sendBleCommand(const char* cmd) {
  if (!bleConnected) {
    Serial.println("BLE not connected — command not sent");
    return;
  }
  pCharacteristic->setValue((uint8_t*)cmd, strlen(cmd));
  pCharacteristic->notify();
  Serial.print("Sent BLE: ");
  Serial.println(cmd);
}

// ── NEXT button (scroll down / next page) ────────────────────────────────
void handleNextButton() {
  if (currentScreen == SCREEN_MENU) {
    selectedItem = (selectedItem + 1) % 2;
  }

  else if (currentScreen == SCREEN_PLAYLISTS) {
    int localIndex = playlistSelected - currentPlaylistPage;
    localIndex++;

    if (localIndex >= livePlaylistCount) {
      // Reached end of this page
      if (livePlaylistCount == 5) {
        // Full page — there may be more, request next page
        currentPlaylistPage += 5;
        char cmd[30];
        sprintf(cmd, "GET_PL_PAGE:%d", currentPlaylistPage);
        sendBleCommand(cmd);
        playlistSelected = currentPlaylistPage;
        isLoadingTracks  = true;
      } else {
        // Partial page — no more playlists, wrap back to first page
        currentPlaylistPage = 0;
        playlistSelected    = 0;
        sendBleCommand("GET_PL_PAGE:0");
        isLoadingTracks = true;
      }
    } else {
      playlistSelected = currentPlaylistPage + localIndex;
    }
  }

  else if (currentScreen == SCREEN_SONG_LIST) {
    int localIndex = globalTrackIndex - currentTrackPage;
    localIndex++;

    if (localIndex >= liveTrackCount) {
      if (liveTrackCount == 5) {
        // Full page — request next
        currentTrackPage += 5;
        char cmd[30];
        sprintf(cmd, "GET_PAGE:%d", currentTrackPage);
        sendBleCommand(cmd);
        globalTrackIndex = currentTrackPage;
        isLoadingTracks  = true;
      } else {
        // Partial page — no more tracks, wrap back to start
        currentTrackPage = 0;
        globalTrackIndex = 0;
        sendBleCommand("GET_PAGE:0");
        isLoadingTracks = true;
      }
    } else {
      globalTrackIndex = currentTrackPage + localIndex;
    }
  }

  redrawRequired = true;
}

// ── UP button (scroll up / previous page) ────────────────────────────────
void handleUpButton() {
  if (currentScreen == SCREEN_MENU) {
    selectedItem = (selectedItem - 1 + 2) % 2;
  }

  else if (currentScreen == SCREEN_PLAYLISTS) {
    int localIndex = playlistSelected - currentPlaylistPage;
    localIndex--;

    if (localIndex < 0) {
      // At top of page — go to previous page if one exists
      if (currentPlaylistPage > 0) {
        currentPlaylistPage -= 5;
        char cmd[30];
        sprintf(cmd, "GET_PL_PAGE:%d", currentPlaylistPage);
        sendBleCommand(cmd);
        playlistSelected = currentPlaylistPage + 4; // Land on last slot of prev page
        isLoadingTracks  = true;
      }
      // Already on the first page — do nothing, stay at top
    } else {
      playlistSelected = currentPlaylistPage + localIndex;
    }
  }

  else if (currentScreen == SCREEN_SONG_LIST) {
    // Special case: we navigated onto an empty page (end of list)
    // Go straight back to the previous page
    if (liveTrackCount == 0) {
      if (currentTrackPage > 0) {
        currentTrackPage -= 5;
        char cmd[30];
        sprintf(cmd, "GET_PAGE:%d", currentTrackPage);
        sendBleCommand(cmd);
        globalTrackIndex = currentTrackPage + 4;
        isLoadingTracks  = true;
      }
      redrawRequired = true;
      return;
    }

    int localIndex = globalTrackIndex - currentTrackPage;
    localIndex--;

    if (localIndex < 0) {
      // At top of page — go to previous page if one exists
      if (currentTrackPage > 0) {
        currentTrackPage -= 5;
        char cmd[30];
        sprintf(cmd, "GET_PAGE:%d", currentTrackPage);
        sendBleCommand(cmd);
        globalTrackIndex = currentTrackPage + 4; // Land on last slot of prev page
        isLoadingTracks  = true;
      }
      // Already on the first page — do nothing, stay at top
    } else {
      globalTrackIndex = currentTrackPage + localIndex;
    }
  }

  redrawRequired = true;
}

// ── SELECT button (confirm) ───────────────────────────────────────────────
void handleSelectButton() {
  if (currentScreen == SCREEN_MENU) {
    if      (selectedItem == 0) currentScreen = SCREEN_NOW_PLAYING;
    else if (selectedItem == 1) {
      currentScreen       = SCREEN_PLAYLISTS;
      currentPlaylistPage = 0;
      playlistSelected    = 0;
    }
  }

  else if (currentScreen == SCREEN_PLAYLISTS) {
    char cmd[20];
    sprintf(cmd, "LOAD_PLAYLIST:%d", playlistSelected);
    sendBleCommand(cmd);

    currentScreen    = SCREEN_SONG_LIST;
    globalTrackIndex = 0;
    currentTrackPage = 0;
    liveTrackCount   = 0;
    isLoadingTracks  = true;
  }

  else if (currentScreen == SCREEN_SONG_LIST) {
    if (!isLoadingTracks) {
      char cmd[20];
      sprintf(cmd, "PLAY_TRACK:%d", globalTrackIndex);
      sendBleCommand(cmd);

      currentSong   = "Loading...";
      currentArtist = "";
      isPlaying     = false; // Pause the old progress bar ticker
      currentProgressSec = 0;

      currentScreen = SCREEN_NOW_PLAYING;
    }
  }

  redrawRequired = true;
}

// ── Screen dispatcher ─────────────────────────────────────────────────────
void drawCurrentScreen() {
  tft.fillScreen(COL_BG);
  if      (currentScreen == SCREEN_MENU)        drawMenuScreen();
  else if (currentScreen == SCREEN_NOW_PLAYING) drawNowPlayingScreen();
  else if (currentScreen == SCREEN_PLAYLISTS)   drawPlaylistsScreen();
  else if (currentScreen == SCREEN_SONG_LIST)   drawSongListScreen();
}

// ── MENU SCREEN ───────────────────────────────────────────────────────────
void drawMenuScreen() {
  drawHeader("Main Menu");
  for (int i = 0; i < 2; i++) {
    drawMenuItem(i, i == selectedItem);
  }
}

// ── NOW PLAYING SCREEN ────────────────────────────────────────────────────
void drawNowPlayingScreen() {
  drawHeader("Now Playing");
  drawBackButton(12, 54, 80, 28, false);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Song:");

  tft.setTextColor(COL_PROGRESS_FG, COL_BG);
  tft.setCursor(90, 100);
  tft.print(currentSong);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(20, 135);
  tft.print("Artist:");

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(100, 135);
  tft.print(currentArtist);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(20, 170);
  tft.print("Volume:");

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(105, 170);
  tft.print(currentVolume);
  tft.print("%");

  int percent = (currentProgressSec * 100) / totalDurationSec;
  drawProgressBar(110, 208, 170, 10, percent);
}


// ── PLAYLISTS SCREEN ──────────────────────────────────────────────────────
void drawPlaylistsScreen() {
  drawHeader("Playlists");

  if (livePlaylistCount == 0) {
    tft.setTextColor(COL_SUBTEXT, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(24, 100);
    tft.print(bleConnected ? "Loading..." : "Waiting for Phone...");
  } else {
    for (int i = 0; i < livePlaylistCount; i++) {
      int y = 54 + i * 34;
      bool isSelected = ((playlistSelected - currentPlaylistPage) == i);
      uint16_t bgCol  = isSelected ? COL_ITEM_SEL : COL_ITEM_BG;

      tft.fillRoundRect(12, y, 296, 28, 6, bgCol);
      tft.drawRoundRect(12, y, 296, 28, 6, COL_HEADER_LINE);
      tft.setTextColor(COL_TEXT, bgCol);
      tft.setTextSize(1);
      tft.setCursor(22, y + 10);
      tft.print(livePlaylists[i]);
    }
  }

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(12, 228);
  tft.print("26: Down | 22: Up | 27: Select (Hold: Back)");
}

// ── SONG LIST SCREEN (PAGINATED) ──────────────────────────────────────────
void drawSongListScreen() {
  drawHeader("Tracks");

  if (isLoadingTracks) {
    tft.setTextColor(COL_SUBTEXT, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(24, 100);
    tft.print("Loading...");
  } else {
    for (int i = 0; i < liveTrackCount; i++) {
      int y = 54 + i * 34;
      bool isSelected = ((globalTrackIndex - currentTrackPage) == i);
      uint16_t bgCol  = isSelected ? COL_ITEM_SEL : COL_ITEM_BG;

      tft.fillRoundRect(12, y, 296, 28, 6, bgCol);
      tft.drawRoundRect(12, y, 296, 28, 6, COL_HEADER_LINE);
      tft.setTextColor(COL_TEXT, bgCol);
      tft.setTextSize(1);
      tft.setCursor(22, y + 10);
      tft.print(liveTracks[i]);
    }
  }

  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(12, 228);
  tft.print("26: Down | 22: Up | 27: Select (Hold: Back)");
}

// ── Header ────────────────────────────────────────────────────────────────
void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COL_HEADER_LINE);

  tft.setTextColor(COL_TEXT, COL_HEADER_BG);
  tft.setTextSize(2);
  int tw = strlen(title) * 12;
  tft.setCursor((SCREEN_W - tw) / 2, (HEADER_H - 16) / 2);
  tft.print(title);

  drawBluetoothIcon(10, HEADER_H / 2, bleConnected);
  drawBattery(SCREEN_W - 52, (HEADER_H - 14) / 2, batteryPct);
}

// ── Menu item ─────────────────────────────────────────────────────────────
void drawMenuItem(int index, bool selected) {
  int y          = ITEMS_Y_START + index * (ITEM_H + ITEM_MARGIN);
  uint16_t bgCol = selected ? COL_ITEM_SEL : COL_ITEM_BG;
  uint16_t txtCol = COL_TEXT;

  tft.fillRoundRect(ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS, bgCol);
  if (!selected)
    tft.drawRoundRect(ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS, COL_HEADER_LINE);

  int iconX = ITEM_X + 16;
  int iconY = y + ITEM_H / 2;
  switch (index) {
    case 0: drawMusicNote(iconX, iconY, txtCol); break;
    case 1: drawListIcon(iconX,  iconY, txtCol); break;
  }

  tft.setTextColor(txtCol, bgCol);
  tft.setTextSize(2);
  tft.setCursor(ITEM_X + 40, y + (ITEM_H - 16) / 2);
  tft.print(menuLabels[index]);

  drawChevron(ITEM_X + ITEM_W - 22, y + ITEM_H / 2,
              selected ? COL_TEXT : COL_ARROW);
}

// ── Back button ───────────────────────────────────────────────────────────
void drawBackButton(int x, int y, int w, int h, bool selected) {
  uint16_t bg = selected ? COL_ITEM_SEL : COL_ITEM_BG;
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.drawRoundRect(x, y, w, h, 6, COL_BACK_BTN);
  tft.setTextColor(COL_TEXT, bg);
  tft.setTextSize(2);
  tft.setCursor(x + 12, y + 6);
  tft.print("< Back");
}

// ── Progress bar ──────────────────────────────────────────────────────────
void drawProgressBar(int x, int y, int w, int h, int percent) {
  tft.fillRoundRect(x, y, w, h, 4, COL_PROGRESS_BG);
  int fillW = (w * percent) / 100;
  if (fillW > 0) tft.fillRoundRect(x, y, fillW, h, 4, COL_PROGRESS_FG);
  tft.drawRoundRect(x, y, w, h, 4, COL_TEXT);
}

// ── Battery ───────────────────────────────────────────────────────────────
void drawBattery(int x, int y, int pct) {
  int bw = 32, bh = 14, tipW = 4, tipH = 6;
  uint16_t fillCol = (pct > 20) ? COL_BATT_FULL : COL_BATT_LOW;

  tft.drawRect(x, y, bw, bh, COL_TEXT);
  tft.fillRect(x + bw, y + (bh - tipH) / 2, tipW, tipH, COL_TEXT);

  int fillW = (bw - 4) * pct / 100;
  if (fillW > 0) tft.fillRect(x + 2, y + 2, fillW, bh - 4, fillCol);

  tft.setTextColor(COL_TEXT, COL_HEADER_BG);
  tft.setTextSize(1);
  char buf[5];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  tft.setCursor(x + bw + tipW + 2, y + 3);
  tft.print(buf);
}

// ── Bluetooth icon ────────────────────────────────────────────────────────
void drawBluetoothIcon(int cx, int cy, bool connected) {
  uint16_t c = connected ? COL_BT_ON : COL_BT_OFF;
  int h = 10;
  tft.drawFastVLine(cx, cy - h, h * 2, c);
  tft.drawLine(cx, cy - h, cx + 5, cy - 5, c);
  tft.drawLine(cx + 5, cy - 5, cx, cy, c);
  tft.drawLine(cx, cy, cx + 5, cy + 5, c);
  tft.drawLine(cx + 5, cy + 5, cx, cy + h, c);
  tft.drawLine(cx, cy - h + 3, cx - 4, cy - h + 7, c);
  tft.drawLine(cx, cy + h - 3, cx - 4, cy + h - 7, c);
}

// ── Chevron ───────────────────────────────────────────────────────────────
void drawChevron(int cx, int cy, uint16_t colour) {
  int s = 6;
  tft.drawLine(cx,     cy - s, cx + s, cy,     colour);
  tft.drawLine(cx + s, cy,     cx,     cy + s, colour);
  tft.drawLine(cx + 1, cy - s, cx + s + 1, cy, colour);
  tft.drawLine(cx + s + 1, cy, cx + 1, cy + s, colour);
}

// ── Icons ─────────────────────────────────────────────────────────────────
void drawMusicNote(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx - 2, cy + 4, 3, c);
  tft.fillCircle(cx + 5, cy + 2, 3, c);
  tft.drawFastVLine(cx + 1, cy - 6, 10, c);
  tft.drawFastVLine(cx + 8, cy - 8, 10, c);
  tft.drawFastHLine(cx + 1, cy - 6, 7, c);
}

void drawListIcon(int cx, int cy, uint16_t c) {
  for (int i = -4; i <= 4; i += 4)
    tft.drawFastHLine(cx - 6, cy + i, 12, c);
}
