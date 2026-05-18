#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// Hardware pin definitions
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_LED_PIN 6
#define TOUCH_CS 7
#define BTN 4

#define LDR_PIN A0
#define RED_LED A1
#define YELLOW_LED A2
#define GREEN_LED A3

// Player and UI colors in RGB565 format
#define PINK 0xF867
#define LIGHTBLUE 0x067F
#define GRID_COLOR 0x4267

// MP3 track indices for specific sound effects
#define WINNING 2
#define PLACING 3
#define SELECTING 4

// Array for random sound selection triggered by the physical button
#define LEN_67 3
int Six_Seven[LEN_67] = { 1, 6, 7 };

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

SoftwareSerial softSerial(2, 3);
DFRobotDFPlayerMini myDFPlayer;

bool lastButtonState = HIGH;

enum GameState { STATE_MENU,
	STATE_DIFFICULTY,
	STATE_GAME,
	STATE_OVER,
	STATE_TEST };
GameState currentState = STATE_MENU;

int gameMode = 0; // 1 for VS CPU, 2 for Player vs Player
int difficulty = 0; // 1 = Easy, 2 = Medium, 3 = Hard
uint8_t board[7][6] = { 0 }; // 7 columns, 6 rows (0 = empty, 1 = Player 1, 2 = Player 2)
int currentPlayer = 1;
unsigned long gameOverTime = 0;

// Circular Buffer for Test Points
#define MAX_TEST_POINTS 30
int testX[MAX_TEST_POINTS];
int testY[MAX_TEST_POINTS];
unsigned long testTime[MAX_TEST_POINTS];
uint16_t testColor[MAX_TEST_POINTS];
bool testFilled[MAX_TEST_POINTS];
int testPointHead = 0;
int testPointCount = 0;

/**
 * Adds a new touch point to the circular buffer and draws it on the screen.
 * If the buffer is full, it erases the oldest point to save memory and screen clutter.
 * 
 * @param x The X coordinate of the touch point.
 * @param y The Y coordinate of the touch point.
 * @param color The 16-bit color to draw the point.
 * @param filled If true, draws a filled circle; otherwise, draws an outline.
 */
void addTestPoint(int x, int y, uint16_t color, bool filled) {
	// If buffer is full, remove the oldest point from the screen
	if (testPointCount == MAX_TEST_POINTS) {
		int oldestIdx = (testPointHead - testPointCount + MAX_TEST_POINTS) % MAX_TEST_POINTS;
		if (testFilled[oldestIdx]) {
			tft.fillCircle(testX[oldestIdx], testY[oldestIdx], 14, ST77XX_BLACK);
		} else {
			tft.drawCircle(testX[oldestIdx], testY[oldestIdx], 14, ST77XX_BLACK);
		}
		testPointCount--;
	}

	// Store new point data in the circular buffer
	testX[testPointHead] = x;
	testY[testPointHead] = y;
	testTime[testPointHead] = millis();
	testColor[testPointHead] = color;
	testFilled[testPointHead] = filled;

	// Draw the new point on the screen
	if (filled) {
		tft.fillCircle(x, y, 14, color);
	} else {
		tft.drawCircle(x, y, 14, color);
	}

	// Advance the head pointer and increment count
	testPointHead = (testPointHead + 1) % MAX_TEST_POINTS;
	testPointCount++;
}

/**
 * Checks the circular buffer for points older than 3000ms.
 * Erases them from the screen to create a fading effect in the TEST state.
 */
void handleTestPointsTimeout() {
	if (currentState != STATE_TEST || testPointCount == 0) return;

	int oldestIdx = (testPointHead - testPointCount + MAX_TEST_POINTS) % MAX_TEST_POINTS;

	// Remove points that have been on screen for more than 3 seconds
	if (millis() - testTime[oldestIdx] >= 3000) {
		if (testFilled[oldestIdx]) {
			tft.fillCircle(testX[oldestIdx], testY[oldestIdx], 14, ST77XX_BLACK);
		} else {
			tft.drawCircle(testX[oldestIdx], testY[oldestIdx], 14, ST77XX_BLACK);
		}
		testPointCount--;

		// Redraw the "BACK" button just in case a point overlapped it
		tft.drawRect(10, 10, 80, 30, ST77XX_WHITE);
		tft.setCursor(18, 17);
		tft.setTextSize(2);
		tft.setTextColor(ST77XX_WHITE);
		tft.print("INAPOI");
	}
}

/**
 * Samples the touchscreen multiple times, filters out noise (spikes),
 * and calculates a stable average X and Y coordinate.
 * 
 * @param outX Reference to store the filtered X coordinate.
 * @param outY Reference to store the filtered Y coordinate.
 * @return True if a valid touch was detected and filtered; False otherwise.
 */
bool getFilteredTouch(int &outX, int &outY) {
	const int NUM_SAMPLES = 32;
	int xSamples[NUM_SAMPLES];
	int ySamples[NUM_SAMPLES];
	int validCount = 0;

	// Collect up to NUM_SAMPLES valid points, discarding low/high pressure outliers (Z-axis)
	for (int i = 0; i < 30 && validCount < NUM_SAMPLES; i++) {
		if (ts.touched()) {
			TS_Point p = ts.getPoint();
			if (p.z > 500 && p.z < 3500) {
				xSamples[validCount] = p.x;
				ySamples[validCount] = p.y;
				validCount++;

				// In TEST state, draw a raw white outline for debugging touch tracking
				if (currentState == STATE_TEST) {
					int rawX = constrain(map(p.x, 300, 3800, 0, 320), 0, 320);
					int rawY = constrain(map(p.y, 300, 3800, 0, 240), 0, 240);
					if (!(rawX > 10 && rawX < 90 && rawY > 10 && rawY < 40)) { // Avoid BACK button area
						addTestPoint(rawX, rawY, ST77XX_WHITE, false);
					}
				}
			}
		}
		delay(2); // Short delay to allow ADC stabilization
	}

	if (validCount < 5) return false; // Not enough samples for a reliable read

	// Insertion sort for X coordinates to prepare for outlier trimming
	for (int i = 1; i < validCount; i++) {
		int keyX = xSamples[i], j = i - 1;
		while (j >= 0 && xSamples[j] > keyX) {
			xSamples[j + 1] = xSamples[j];
			j--;
		}
		xSamples[j + 1] = keyX;
	}
	
	// Insertion sort for Y coordinates
	for (int i = 1; i < validCount; i++) {
		int keyY = ySamples[i], j = i - 1;
		while (j >= 0 && ySamples[j] > keyY) {
			ySamples[j + 1] = ySamples[j];
			j--;
		}
		ySamples[j + 1] = keyY;
	}

	// Trim the top and bottom 20% of values to remove extreme noise
	int trimAmount = validCount / 5;
	long sumX = 0, sumY = 0;
	int count = 0;
	for (int i = trimAmount; i < validCount - trimAmount; i++) {
		sumX += xSamples[i];
		sumY += ySamples[i];
		count++;
	}
	if (count == 0) return false;

	// Map raw ADC values to screen pixel coordinates
	outX = constrain(map(sumX / count, 300, 3800, 0, 320), 0, 320);
	outY = constrain(map(sumY / count, 300, 3800, 0, 240), 0, 240);
	return true;
}

/**
 * Finds the first available empty row in a given column.
 * 
 * @param col The column index (0-6) to check.
 * @return The row index (0-5) where a piece would land. Returns 6 if column is full.
 */
int getTopRow(int col) {
	int r = 0;
	while (r < 6 && board[col][r] != 0) r++;
	return r;
}

/**
 * Scans the entire board to determine if any player has aligned 4 pieces.
 * 
 * @return The ID of the winning player (1 or 2). Returns 0 if no winner.
 */
int checkWinLogic() {
	// Check horizontal wins
	for (int r = 0; r < 6; r++)
		for (int c = 0; c < 4; c++)
			if (board[c][r] != 0 && board[c][r] == board[c + 1][r] && board[c][r] == board[c + 2][r] && board[c][r] == board[c + 3][r]) return board[c][r];
			
	// Check vertical wins
	for (int c = 0; c < 7; c++)
		for (int r = 0; r < 3; r++)
			if (board[c][r] != 0 && board[c][r] == board[c][r + 1] && board[c][r] == board[c][r + 2] && board[c][r] == board[c][r + 3]) return board[c][r];
			
	// Check diagonal (bottom-left to top-right) wins
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 3; r++)
			if (board[c][r] != 0 && board[c][r] == board[c + 1][r + 1] && board[c][r] == board[c + 2][r + 2] && board[c][r] == board[c + 3][r + 3]) return board[c][r];
			
	// Check diagonal (top-left to bottom-right) wins
	for (int c = 0; c < 4; c++)
		for (int r = 3; r < 6; r++)
			if (board[c][r] != 0 && board[c][r] == board[c + 1][r - 1] && board[c][r] == board[c + 2][r - 2] && board[c][r] == board[c + 3][r - 3]) return board[c][r];
	
	return 0;
}

/**
 * Fast function to recognize MCTS (Monte Carlo Tree Search) specific building patterns.
 * Specifically, it looks for a sliding window of 4 spaces containing exactly 3 friendly pieces and 1 empty space.
 * 
 * @param player The ID of the player (1 or 2) to evaluate.
 * @return The total number of "lines of 3" threats found on the board.
 */
int countLinesOf3(int player) {
	int lines = 0;
	
	// Horizontal windows
	for (int r = 0; r < 6; r++) {
		for (int c = 0; c < 4; c++) {
			int p = 0, e = 0;
			for (int i = 0; i < 4; i++) {
				if (board[c + i][r] == player) p++;
				else if (board[c + i][r] == 0) e++;
			}
			if (p == 3 && e == 1) lines++;
		}
	}
	
	// Vertical windows
	for (int c = 0; c < 7; c++) {
		for (int r = 0; r < 3; r++) {
			int p = 0, e = 0;
			for (int i = 0; i < 4; i++) {
				if (board[c][r + i] == player) p++;
				else if (board[c][r + i] == 0) e++;
			}
			if (p == 3 && e == 1) lines++;
		}
	}
	
	// Diagonal windows (bottom-left to top-right)
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 3; r++) {
			int p = 0, e = 0;
			for (int i = 0; i < 4; i++) {
				if (board[c + i][r + i] == player) p++;
				else if (board[c + i][r + i] == 0) e++;
			}
			if (p == 3 && e == 1) lines++;
		}
	}
	
	// Diagonal windows (top-left to bottom-right)
	for (int c = 0; c < 4; c++) {
		for (int r = 3; r < 6; r++) {
			int p = 0, e = 0;
			for (int i = 0; i < 4; i++) {
				if (board[c + i][r - i] == player) p++;
				else if (board[c + i][r - i] == 0) e++;
			}
			if (p == 3 && e == 1) lines++;
		}
	}
	return lines;
}

/**
 * Calculates a heuristic score for dropping a piece in a specific column.
 * Considers immediate wins, blocks, tactical forks, and center-column preference.
 * 
 * @param col The column index to simulate the move.
 * @param player The ID of the player making the move.
 * @param diff The difficulty level, determining how deep the heuristic looks.
 * @return A numerical score representing the desirability of the move.
 */
long scoreMove(int col, int player, int diff) {
	int r = getTopRow(col);
	if (r == 6) return -999999; // Invalid move (column full)
	
	int opp = (player == 1) ? 2 : 1;
	// Base score: favor center columns
	long score = (3 - abs(3 - col)) * 10;

	// 1. Check if this move wins the game immediately
	board[col][r] = player;
	if (checkWinLogic() == player) {
		board[col][r] = 0;
		return 100000;
	}

	// 2. TACTICAL BONUS: MCTS lite mechanism (diff >= 2)
	// Give a significant bonus if the move creates a new 3-in-a-row threat
	if (diff >= 2) {
		score += countLinesOf3(player) * 500;
	}
	board[col][r] = 0;

	// 3. BLOCKING: Check if the opponent would win here next turn, and block it
	if (diff >= 2) {
		board[col][r] = opp;
		if (checkWinLogic() == opp) score += 90000;
		board[col][r] = 0;
	}

	// 4. DANGER AVOIDANCE: Check if our move allows the opponent to win right on top of it
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

	// 5. ADVANCED TACTICS: Fork creation and fork prevention (diff == 3)
	if (diff == 3 && !givesWin) {
		int myWinPaths = 0;
		// Count how many immediate winning moves we have after this move
		for (int c = 0; c < 7; c++) {
			int orow = getTopRow(c);
			if (orow < 6) {
				board[c][orow] = player;
				if (checkWinLogic() == player) myWinPaths++;
				board[c][orow] = 0;
			}
		}
		if (myWinPaths >= 2) score += 50000; // Created a double-threat (fork)

		// Check if this move allows the opponent to create a fork next turn
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
	
	// Revert simulation and return score
	board[col][r] = 0;
	return score;
}

/**
 * Evaluates the human player's move quality to provide LED feedback.
 * 
 * @param col The column chosen by the human player.
 * @param player The ID of the human player.
 * @return An integer (1, 2, or 3) mapping to RED, YELLOW, or GREEN LED states.
 */
int evaluateMove(int col, int player) {
	int r = getTopRow(col);
	if (r == 6) return 0; // Invalid move
	
	int opp = (player == 1) ? 2 : 1;
	long moveScore = scoreMove(col, player, 3);

	// Check if the opponent had an immediate winning threat before our move
	bool oppHadThreat = false;
	for (int c = 0; c < 7; c++) {
		int orow = getTopRow(c);
		if (orow < 6) {
			board[c][orow] = opp;
			if (checkWinLogic() == opp) oppHadThreat = true;
			board[c][orow] = 0;
		}
	}

	int myLinesBefore = countLinesOf3(player);	// Count initial 3-in-a-row patterns

	// Simulate move to see if we handed the opponent a win
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
	int myLinesAfter = countLinesOf3(player);	// Count patterns after move
	board[col][r] = 0;

	// Quality classification
	if (moveScore >= 100000) return 3; // Excellent (Winning move)
	if (oppHadThreat && oppCanWinNext) return 1; // Bad (Missed a block)
	if (moveScore >= 90000) return 3; // Excellent (Successfully blocked opponent)
	if (moveScore <= -30000 || oppCanWinNext) return 1; // Bad (Blundered and gave opponent a win)
	if (moveScore >= 50000) return 3; // Excellent (Created a fork)

	// GREEN LED condition: Move completed a strong tactical pattern (e.g., 3 in a row)
	if (myLinesAfter > myLinesBefore) return 3;

	return 2; // Neutral / Yellow (Standard developing move)
}

/**
 * Controls the physical feedback LEDs based on move evaluation.
 * 
 * @param state -1/0/1 = RED (Bad), 2 = YELLOW (Neutral), 3 = GREEN (Excellent).
 */
void setLEDs(int state) {
	digitalWrite(RED_LED, LOW);
	digitalWrite(YELLOW_LED, LOW);
	digitalWrite(GREEN_LED, LOW);
	if (state == 0 || state == 1) digitalWrite(RED_LED, HIGH);
	else if (state == 2) digitalWrite(YELLOW_LED, HIGH);
	else if (state == 3) digitalWrite(GREEN_LED, HIGH);
}

/**
 * Computes the best move for the CPU using the heuristic scoring system.
 * If multiple columns share the top score, it randomly selects one to add variance.
 * 
 * @return The index of the optimal column (0-6) for the CPU.
 */
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
				bestCols[bestCount++] = c; // Tie found, add to candidates
			}
		}
	}
	// Randomly pick among equivalent best moves
	return bestCols[random(0, bestCount)];
}

/**
 * Renders the main menu UI, offering VS CPU, 2 Players, and Test mode.
 */
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

	tft.drawRect(240, 10, 70, 30, ST77XX_WHITE);
	tft.setCursor(249, 18);
	tft.print("TEST");
}

/**
 * Renders the difficulty selection menu for VS CPU mode.
 */
void drawDifficultyMenu() {
	tft.fillScreen(ST77XX_BLACK);
	tft.setTextColor(ST77XX_WHITE);
	tft.setTextSize(2);
	tft.setCursor(50, 30);
	tft.print("ALEGE DIFICULTATEA:");

	tft.drawRect(60, 70, 200, 40, ST77XX_WHITE);
	tft.setCursor(130, 82);
	tft.print("USOR");
	tft.drawRect(60, 120, 200, 40, ST77XX_WHITE);
	tft.setCursor(125, 132);
	tft.print("MEDIU");
	tft.drawRect(60, 170, 200, 40, ST77XX_WHITE);
	tft.setCursor(125, 182);
	tft.print("GREU");
}

/**
 * Updates the top banner to show which player's turn it is.
 */
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

/**
 * Displays the final game over banner and announces the winner.
 * 
 * @param winner The ID of the winning player.
 */
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

/**
 * Clears the screen and draws the empty 7x6 Connect 4 grid.
 */
void drawEmptyGrid() {
	tft.fillScreen(ST77XX_BLACK);
	drawTurnIndicator();
	for (int col = 0; col < 7; col++) {
		for (int row = 0; row < 6; row++) {
			tft.drawCircle(col * 45 + 25, 240 - (row * 35 + 17), 14, GRID_COLOR);
		}
	}
}

/**
 * Animates a game piece falling into its designated slot.
 * 
 * @param col The column where the piece is dropped.
 * @param target_row The row where the piece will eventually land.
 * @param player The ID of the player, determining the color of the piece.
 */
void animateDrop(int col, int target_row, int player) {
	int targetY = 240 - (target_row * 35 + 17);
	int startX = col * 45 + 25;
	uint16_t color = (player == 1) ? PINK : LIGHTBLUE;

	// Falling animation loop
	for (int y = 25; y < targetY; y += 12) {
		tft.fillCircle(startX, y, 14, color);
		delay(15);
		tft.fillCircle(startX, y, 14, ST77XX_BLACK); // Erase previous frame
		
		// Redraw grid outlines that the falling piece might have overwritten
		for (int r = 5; r > target_row; r--) {
			int gridY = 240 - (r * 35 + 17);
			if (abs(y - gridY) < 32) tft.drawCircle(startX, gridY, 14, GRID_COLOR);
		}
	}
	// Draw final settled piece and update logic board
	tft.fillCircle(startX, targetY, 14, color);
	board[col][target_row] = player;
}

/**
 * Resets the game board array, UI, and variables for a new match.
 */
void resetGame() {
	memset(board, 0, sizeof(board));
	currentPlayer = 1;
	setLEDs(-1);
	drawEmptyGrid();
}

/**
 * Arduino setup function. Initializes serial comms, pins, display, touch, and audio.
 */
void setup() {
	Serial.begin(9600);
	softSerial.begin(9600);
	
	// Pin configurations
	pinMode(TFT_LED_PIN, OUTPUT);
	pinMode(BTN, INPUT_PULLUP);
	pinMode(RED_LED, OUTPUT);
	pinMode(YELLOW_LED, OUTPUT);
	pinMode(GREEN_LED, OUTPUT);
	setLEDs(-1);

	// Display and Touch initialization
	tft.init(240, 320);
	tft.setRotation(1); // Landscape
	tft.invertDisplay(false);
	ts.begin();
	ts.setRotation(0);

	// MP3 Player initialization
	if (myDFPlayer.begin(softSerial, false, false)) {
		delay(500);
		myDFPlayer.volume(16);
	}
	
	randomSeed(micros());
	drawMenu();
}

/**
 * Main Arduino loop. Handles async events and manages the game state machine.
 */
void loop() {
	// 1. AUDIO BUTTON LOGIC
	// Triggers a random sound effect on falling edge of the button press
	bool currentButtonState = digitalRead(BTN);
	if (lastButtonState == HIGH && currentButtonState == LOW) {
		delay(50); // Debounce
		myDFPlayer.playMp3Folder(Six_Seven[random(0, LEN_67)]);
	}
	lastButtonState = currentButtonState;

	// 2. BACKLIGHT PWM WITH ANTI-CROSSTALK FILTER
	// Reads LDR, maps to PWM, and applies double reading to clear ADC multiplexer crosstalk
	analogRead(LDR_PIN);
	delay(2);
	int luminaInitiala = analogRead(LDR_PIN);
	int luminozitatePWM = map(luminaInitiala, 180, 980, 16, 255);
	analogWrite(TFT_LED_PIN, constrain(luminozitatePWM, 16, 255));

	// Fetch stable touch coordinates if available
	int tx, ty;
	bool touched = getFilteredTouch(tx, ty);

	// Process timeout for disappearing touch test points
	if (currentState == STATE_TEST) {
		handleTestPointsTimeout();
	}

	// 3. MAIN GAME STATE MACHINE
	switch (currentState) {
		case STATE_MENU:
			if (touched) {
				if (tx > 40 && tx < 280) {
					// VS CPU Button
					if (ty > 100 && ty < 140) {
						myDFPlayer.playMp3Folder(SELECTING);
						gameMode = 1;
						currentState = STATE_DIFFICULTY;
						drawDifficultyMenu();
						delay(300);
					} 
					// 2 PLAYERS Button
					else if (ty > 160 && ty < 200) {
						myDFPlayer.playMp3Folder(SELECTING);
						gameMode = 2;
						currentState = STATE_GAME;
						resetGame();
						delay(300);
					}
				}
				// TEST Button
				if (tx > 240 && tx < 310 && ty > 10 && ty < 40) {
					myDFPlayer.playMp3Folder(SELECTING);
					currentState = STATE_TEST;
					testPointCount = 0;
					testPointHead = 0;
					tft.fillScreen(ST77XX_BLACK);
					tft.drawRect(10, 10, 80, 30, ST77XX_WHITE);
					tft.setCursor(18, 17);
					tft.setTextSize(2);
					tft.print("INAPOI"); // "BACK"
					delay(300);
				}
			}
			break;

		case STATE_DIFFICULTY:
			if (touched && tx > 60 && tx < 260) {
				bool buttonPressed = false;
				// Easy
				if (ty > 70 && ty < 110) {
					difficulty = 1;
					buttonPressed = true;
				} 
				// Medium
				else if (ty > 120 && ty < 160) {
					difficulty = 2;
					buttonPressed = true;
				} 
				// Hard
				else if (ty > 170 && ty < 210) {
					difficulty = 3;
					buttonPressed = true;
				}

				// If a valid difficulty was selected, start the match
				if (buttonPressed) {
					myDFPlayer.playMp3Folder(SELECTING);
					currentState = STATE_GAME;
					resetGame();
				}
				delay(300);
			}
			break;

		case STATE_TEST:
			if (touched) {
				// BACK button logic
				if (tx > 10 && tx < 90 && ty > 10 && ty < 40) {
					myDFPlayer.playMp3Folder(SELECTING);
					currentState = STATE_MENU;
					drawMenu();
					delay(300);
				} else {
					// Draw a red filled circle representing the stable touch read
					addTestPoint(tx, ty, ST77XX_RED, true);
					delay(30);
				}
			}
			break;

		case STATE_GAME:
			// CPU Turn Logic
			if (gameMode == 1 && currentPlayer == 2) {
				setLEDs(-1); // Turn off evaluation LEDs while CPU is thinking
				delay(600); // Artificial delay to make CPU feel "human"
				int cpuCol = makeCPUMove();
				int r = getTopRow(cpuCol);

				myDFPlayer.playMp3Folder(PLACING);
				animateDrop(cpuCol, r, 2);

				int win = checkWinLogic();
				if (win != 0) {
					currentState = STATE_OVER;
					gameOverTime = millis();
					drawGameOver(win);
					delay(200);
					myDFPlayer.playMp3Folder(WINNING);
				} else {
					currentPlayer = 1; // Pass turn back to human
					drawTurnIndicator();
				}
			} 
			// Human Player Turn Logic
			else if (touched) {
				int col = 0;
				int minDistance = 1000;
				
				// Map physical touch X coordinate to the nearest grid column
				for (int c = 0; c < 7; c++) {
					int circleCenterX = c * 45 + 25;
					int distance = abs(tx - circleCenterX);
					if (distance < minDistance) {
						minDistance = distance;
						col = c;
					}
				}

				// Provide LED feedback on the move quality before placing it
				int moveQuality = evaluateMove(col, currentPlayer);
				setLEDs(moveQuality);

				// If move is valid (column not full)
				if (moveQuality != 0) {
					int r = getTopRow(col);
					myDFPlayer.playMp3Folder(PLACING);
					animateDrop(col, r, currentPlayer);

					int win = checkWinLogic();
					if (win != 0) {
						currentState = STATE_OVER;
						gameOverTime = millis();
						drawGameOver(win);
						delay(200);
						myDFPlayer.playMp3Folder(WINNING);
					} else {
						// Switch turn
						currentPlayer = (currentPlayer == 1) ? 2 : 1;
						drawTurnIndicator();
						// Clear LEDs if it's the CPU's turn next
						if (gameMode == 1 && currentPlayer == 2) setLEDs(-1);
					}
					delay(300);
				}
			}
			break;

		case STATE_OVER:
			// Wait for 2 seconds on the game over screen before returning to menu
			if (millis() - gameOverTime > 2000) {
				setLEDs(-1);
				currentState = STATE_MENU;
				drawMenu();
			}
			break;
	}
}