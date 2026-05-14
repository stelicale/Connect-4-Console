#include <Adafruit_GFX.h>    
#include <Adafruit_ST7789.h> 
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8 
#define TFT_LED_PIN 6
#define TOUCH_CS 7
#define BTN 4

#define LDR_PIN A0
#define RED_LED A1
#define YELLOW_LED A2
#define GREEN_LED A3

// Culorile jucătorilor
#define PINK       0xF867
#define LIGHTBLUE  0x067F
#define GRID_COLOR 0x4267 

// Sunetele MP3
#define SIX_SEVEN 1
#define WINNING 2
#define PLACING 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

SoftwareSerial softSerial(2, 3);
DFRobotDFPlayerMini myDFPlayer;

bool lastButtonState = HIGH;   

// --- Stări și Variabile Joc ---
enum GameState { STATE_MENU, STATE_DIFFICULTY, STATE_GAME, STATE_OVER };
GameState currentState = STATE_MENU;

int gameMode = 0; 
int difficulty = 0; 
uint8_t board[7][6] = {0}; 
int currentPlayer = 1;     
unsigned long gameOverTime = 0;


// =========================================================================
// FILTRU TOUCH
// =========================================================================
bool getFilteredTouch(int &outX, int &outY) {
  const int NUM_SAMPLES = 11; 
  int xSamples[NUM_SAMPLES];
  int ySamples[NUM_SAMPLES];
  int validCount = 0;

  for (int i = 0; i < 30 && validCount < NUM_SAMPLES; i++) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      if (p.z > 500 && p.z < 3500) {
        xSamples[validCount] = p.x; ySamples[validCount] = p.y;
        validCount++;
      }
    }
    delay(2);
  }
  if (validCount < 5) return false;

  for (int i = 1; i < validCount; i++) {
    int keyX = xSamples[i], j = i - 1;
    while (j >= 0 && xSamples[j] > keyX) { xSamples[j + 1] = xSamples[j]; j--; }
    xSamples[j + 1] = keyX;
  }
  for (int i = 1; i < validCount; i++) {
    int keyY = ySamples[i], j = i - 1;
    while (j >= 0 && ySamples[j] > keyY) { ySamples[j + 1] = ySamples[j]; j--; }
    ySamples[j + 1] = keyY;
  }

  int trimAmount = validCount / 5; 
  long sumX = 0, sumY = 0;
  int count = 0;
  for (int i = trimAmount; i < validCount - trimAmount; i++) {
    sumX += xSamples[i]; sumY += ySamples[i]; count++;
  }
  if (count == 0) return false;

  outX = constrain(map(sumX / count, 300, 3800, 0, 320), 0, 320);
  outY = constrain(map(sumY / count, 300, 3800, 0, 240), 0, 240);
  return true;
}

// =========================================================================
// LOGICA DE BAZĂ ȘI EVALUARE (MINIMAX HEURISTIC)
// =========================================================================

// Găsește primul rând liber dintr-o coloană (0..5). Dacă e plin returnează 6.
int getTopRow(int col) {
  int r = 0;
  while (r < 6 && board[col][r] != 0) r++;
  return r;
}

int checkWinLogic() {
  for (int r = 0; r < 6; r++) 
    for (int c = 0; c < 4; c++) 
      if (board[c][r] != 0 && board[c][r] == board[c+1][r] && board[c][r] == board[c+2][r] && board[c][r] == board[c+3][r]) return board[c][r];
  for (int c = 0; c < 7; c++) 
    for (int r = 0; r < 3; r++) 
      if (board[c][r] != 0 && board[c][r] == board[c][r+1] && board[c][r] == board[c][r+2] && board[c][r] == board[c][r+3]) return board[c][r];
  for (int c = 0; c < 4; c++) 
    for (int r = 0; r < 3; r++) 
      if (board[c][r] != 0 && board[c][r] == board[c+1][r+1] && board[c][r] == board[c+2][r+2] && board[c][r] == board[c+3][r+3]) return board[c][r];
  for (int c = 0; c < 4; c++) 
    for (int r = 3; r < 6; r++) 
      if (board[c][r] != 0 && board[c][r] == board[c+1][r-1] && board[c][r] == board[c+2][r-2] && board[c][r] == board[c+3][r-3]) return board[c][r];
  return 0;
}

// Scorul unei mutări (folosit și de CPU și de evaluarea LED-urilor)
long scoreMove(int col, int player, int diff) {
  int r = getTopRow(col);
  if (r == 6) return -999999; // Coloană plină

  int opp = (player == 1) ? 2 : 1;
  long score = (3 - abs(3 - col)) * 10; // Prioritizare centru (0..30 puncte)

  // 1. CÂȘTIGĂM IMEDIAT?
  board[col][r] = player;
  if (checkWinLogic() == player) { board[col][r] = 0; return 100000; }
  board[col][r] = 0;

  // 2. BLOCĂM ADVERSARUL? (Ignorat de CPU Usor, dar luat in calcul la restul)
  if (diff >= 2) {
    board[col][r] = opp;
    if (checkWinLogic() == opp) score += 90000;
    board[col][r] = 0;
  }

  // 3. EVITĂM SĂ DĂM UN CÂȘTIG ADVERSARULUI (Blunder)
  board[col][r] = player;
  bool givesWin = false;
  for (int c = 0; c < 7; c++) {
    int orow = getTopRow(c);
    if (orow < 6) {
      board[c][orow] = opp;
      if (checkWinLogic() == opp) givesWin = true;
      board[c][orow] = 0;
    }
  }
  if (givesWin) score -= 80000;

  // 4. LOGICA DE FURCULIȚE (FORKS) - Doar la nivel GREU sau pt analiza LED
  if (diff == 3 && !givesWin) {
    // Furculița Noastră (Să o creăm)
    int myWinPaths = 0;
    for (int c = 0; c < 7; c++) {
      int orow = getTopRow(c);
      if (orow < 6) {
        board[c][orow] = player;
        if (checkWinLogic() == player) myWinPaths++;
        board[c][orow] = 0;
      }
    }
    if (myWinPaths >= 2) score += 50000;

    // Furculița Adversarului (Să NU i-o oferim lui)
    bool givesFork = false;
    for (int c = 0; c < 7; c++) {
      int orow = getTopRow(c);
      if (orow < 6) {
        board[c][orow] = opp;
        int oppWinPaths = 0;
        for (int c2 = 0; c2 < 7; c2++) {
          int orow2 = getTopRow(c2);
          if (orow2 < 6) {
            board[c2][orow2] = opp;
            if (checkWinLogic() == opp) oppWinPaths++;
            board[c2][orow2] = 0;
          }
        }
        if (oppWinPaths >= 2) givesFork = true;
        board[c][orow] = 0;
      }
    }
    if (givesFork) score -= 40000;
  }
  board[col][r] = 0;
  return score;
}

// Analizează mutarea curentă a jucătorului pentru a aprinde LED-urile
int evaluateMove(int col, int player) {
  int r = getTopRow(col);
  if (r == 6) return 0; // Invalid / Plin

  int opp = (player == 1) ? 2 : 1;
  
  // Aflăm ce s-ar fi întâmplat dacă jucam la capacitate GREA (3)
  long moveScore = scoreMove(col, player, 3);

  // Are adversarul deja un câștig pe masă pe care noi NU îl blocăm?
  bool oppHadThreat = false;
  for (int c = 0; c < 7; c++) {
    int orow = getTopRow(c);
    if (orow < 6) {
      board[c][orow] = opp;
      if (checkWinLogic() == opp) oppHadThreat = true;
      board[c][orow] = 0;
    }
  }
  
  // Aplicăm temporar mutarea noastră
  board[col][r] = player;
  bool oppCanWinNext = false;
  for (int c = 0; c < 7; c++) {
    int orow = getTopRow(c);
    if (orow < 6) {
      board[c][orow] = opp;
      if (checkWinLogic() == opp) oppCanWinNext = true;
      board[c][orow] = 0;
    }
  }
  board[col][r] = 0;

  // Analiza Finală LED:
  if (moveScore >= 100000) return 3; // VERDE: Noi câștigăm!
  if (oppHadThreat && oppCanWinNext) return 1; // ROȘU: A ignorat o amenințare letală
  if (moveScore >= 90000) return 3; // VERDE: Am blocat un câștig al inamicului!
  if (moveScore <= -30000 || oppCanWinNext) return 1; // ROȘU: Dă inamicului un câștig iminent sau un Fork
  if (moveScore >= 50000) return 3; // VERDE: Ne-am creat un Fork!

  return 2; // GALBEN: Mutare sigură
}


void setLEDs(int state) {
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  
  if (state == 0 || state == 1) digitalWrite(RED_LED, HIGH); 
  else if (state == 2) digitalWrite(YELLOW_LED, HIGH);       
  else if (state == 3) digitalWrite(GREEN_LED, HIGH);        
}

// =========================================================================
// CPU LOGIC INTEGRATION
// =========================================================================
int makeCPUMove() {
  int bestCols[7];
  int bestCount = 0;
  long bestScore = -9999999;

  for (int c = 0; c < 7; c++) {
    if (getTopRow(c) < 6) {
      long currentScore = scoreMove(c, 2, difficulty);
      if (currentScore > bestScore) {
        bestScore = currentScore;
        bestCount = 0;
        bestCols[bestCount++] = c;
      } else if (currentScore == bestScore) {
        // Păstrăm multiple variante bune pentru o ușoară variație la același scor
        bestCols[bestCount++] = c;
      }
    }
  }
  // Alege random dintre mutările la fel de logice/bune
  return bestCols[random(0, bestCount)];
}


// =========================================================================
// INTERFAȚĂ ȘI ANIMAȚII
// =========================================================================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(60, 40);
  tft.print("CONNECT 4");
  
  tft.setTextSize(2);
  tft.drawRect(40, 100, 240, 40, ST77XX_WHITE);
  tft.setCursor(95, 112);
  tft.print("VS CPU");
  
  tft.drawRect(40, 160, 240, 40, ST77XX_WHITE);
  tft.setCursor(85, 172);
  tft.print("2 JUCATORI");
}

void drawDifficultyMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(50, 30);
  tft.print("ALEGE DIFICULTATEA:");
  
  tft.drawRect(60, 70, 200, 40, ST77XX_WHITE);
  tft.setCursor(130, 82); tft.print("USOR");
  
  tft.drawRect(60, 120, 200, 40, ST77XX_WHITE);
  tft.setCursor(125, 132); tft.print("MEDIU");
  
  tft.drawRect(60, 170, 200, 40, ST77XX_WHITE);
  tft.setCursor(125, 182); tft.print("GREU");
}

void drawTurnIndicator() {
  tft.fillRect(0, 0, 320, 25, ST77XX_BLACK); 
  tft.setCursor(10, 5);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Urmeaza: ");
  if (currentPlayer == 1) {
    tft.setTextColor(PINK);
    tft.print("ROZ");
  } else {
    tft.setTextColor(LIGHTBLUE);
    tft.print("ALBASTRU");
  }
}

void drawGameOver(int winner) {
  tft.fillRect(0, 0, 320, 25, ST77XX_BLACK);
  tft.setCursor(10, 5);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("CASTIGATOR: ");
  if (winner == 1) {
    tft.setTextColor(PINK);
    tft.print("ROZ");
  } else if (winner == 2) {
    tft.setTextColor(LIGHTBLUE);
    tft.print("ALBASTRU");
  }
}

void drawEmptyGrid() {
  tft.fillScreen(ST77XX_BLACK);
  drawTurnIndicator();
  for (int col = 0; col < 7; col++) {
    for (int row = 0; row < 6; row++) {
      int cx = col * 45 + 25;
      int cy = 240 - (row * 35 + 17);
      tft.drawCircle(cx, cy, 14, GRID_COLOR);
    }
  }
}

void animateDrop(int col, int target_row, int player) {
  int targetY = 240 - (target_row * 35 + 17);
  int startX = col * 45 + 25;
  uint16_t color = (player == 1) ? PINK : LIGHTBLUE;

  for (int y = 25; y < targetY; y += 12) {
    tft.fillCircle(startX, y, 14, color);
    delay(15); 
    tft.fillCircle(startX, y, 14, ST77XX_BLACK); 
    for (int r = 5; r > target_row; r--) {
      int gridY = 240 - (r * 35 + 17);
      if (abs(y - gridY) < 32) tft.drawCircle(startX, gridY, 14, GRID_COLOR);
    }
  }
  tft.fillCircle(startX, targetY, 14, color);
  board[col][target_row] = player;
}

void resetGame() {
  memset(board, 0, sizeof(board));
  currentPlayer = 1; 
  setLEDs(-1); 
  drawEmptyGrid();
}

// =========================================================================
void setup() {
  Serial.begin(9600); 
  softSerial.begin(9600);

  pinMode(TFT_LED_PIN, OUTPUT);
  pinMode(BTN, INPUT_PULLUP);  
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  setLEDs(-1); 

  tft.init(240, 320); 
  tft.setRotation(1); 
  tft.invertDisplay(false); 
  
  ts.begin();
  ts.setRotation(0); 

  if (myDFPlayer.begin(softSerial, false, false)) {
    // PAUZA VITALĂ: Îi dăm timp modulului MP3 să se trezească
    delay(500);
    myDFPlayer.volume(13); 
  }

  // Folosim randomSeed pe un pin analog liber neconectat (A4) pentru a ne asigura că 
  // CPU-ul va juca diferit la fiecare pornire a consolei.
  randomSeed(analogRead(A4));

  drawMenu();
}

void loop() {
  // 1. BUTON AUDIO
  bool currentButtonState = digitalRead(BTN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(50); 
    myDFPlayer.playMp3Folder(SIX_SEVEN); 
  }
  lastButtonState = currentButtonState;

  // 2. LUMINĂ
  int luminaInitiala = analogRead(LDR_PIN);
  int luminozitatePWM = map(luminaInitiala, 180, 980, 16, 255);
  analogWrite(TFT_LED_PIN, constrain(luminozitatePWM, 16, 255));

  int tx, ty;
  bool touched = getFilteredTouch(tx, ty);

  // 3. MAȘINA DE STĂRI A JOCULUI
  switch (currentState) {
    case STATE_MENU:
      if (touched) {
        if (tx > 40 && tx < 280) {
          if (ty > 100 && ty < 140) { gameMode = 1; currentState = STATE_DIFFICULTY; drawDifficultyMenu(); }
          else if (ty > 160 && ty < 200) { gameMode = 2; currentState = STATE_GAME; resetGame(); }
          delay(300);
        }
      }
      break;

    case STATE_DIFFICULTY:
      if (touched && tx > 60 && tx < 260) {
        if (ty > 70 && ty < 110) difficulty = 1;
        else if (ty > 120 && ty < 160) difficulty = 2;
        else if (ty > 170 && ty < 210) difficulty = 3;
        if (difficulty > 0) { currentState = STATE_GAME; resetGame(); }
        delay(300);
      }
      break;

    case STATE_GAME:
      myDFPlayer.playMp3Folder(PLACING);
      if (gameMode == 1 && currentPlayer == 2) {
        setLEDs(-1); 
        delay(600); 
        
        int cpuCol = makeCPUMove();
        int r = getTopRow(cpuCol);
        animateDrop(cpuCol, r, 2);
        
        int win = checkWinLogic();
        if (win != 0) {
          currentState = STATE_OVER;
          gameOverTime = millis();
          drawGameOver(win);
        } else {
          currentPlayer = 1;
          drawTurnIndicator();
        }
      } 
      else if (touched) {
        int col = constrain(tx / 45, 0, 6);
        
        int moveQuality = evaluateMove(col, currentPlayer);
        setLEDs(moveQuality);

        if (moveQuality != 0) { // Orice nu e 0 înseamnă mutare validă din punct de vedere fizic (coloană ne-plină)
          int r = getTopRow(col);
          animateDrop(col, r, currentPlayer);
          
          int win = checkWinLogic();
          if (win != 0) {
            currentState = STATE_OVER;
            gameOverTime = millis();
            drawGameOver(win);
            delay(200);
            myDFPlayer.playMp3Folder(WINNING);
          } else {
            currentPlayer = (currentPlayer == 1) ? 2 : 1;
            drawTurnIndicator();
            
            if (gameMode == 1 && currentPlayer == 2) setLEDs(-1);
          }
          delay(300);
        }
      }
      break;

    case STATE_OVER:
      if (millis() - gameOverTime > 2000) { 
        setLEDs(-1); 
        currentState = STATE_MENU; 
        drawMenu(); 
      }
      break;
  }
}