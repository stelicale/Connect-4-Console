# Connect-4-Console
PM Fair

## Introducere
Proiectul constă în realizarea unei console hardware independente pe care rulează clasicul joc de societate. 
  * **Ce face:** Permite unui jucător să joace Connect 4 împotriva "CPU" (sau în mod 2 jucători) pe un ecran TFT tactil. Jocul adaptează automat luminozitatea ecranului în funcție de mediul ambiant, oferă feedback vizual (LED-uri) pentru calitatea mutărilor și este însoțit de efecte sonore redate printr-un modul MP3 dedicat.
  * **Scopul lui:** Replicarea fizică a unui joc de strategie cunoscut, combinând o interfață grafică digitală interactivă (touchscreen) cu feedback audio-vizual pentru o experiență completă.
  * **Ideea de la care am pornit:** Mi-am dorit să aduc un joc al copilăriei în format digital, construind o mini-consolă portabilă. Am vrut să explorez modul în care un microcontroller poate gestiona simultan grafică, preluarea precisă a atingerilor pe ecran, un oponent CPU (AI) și redare audio.
  * **Utilitate:** Este un proiect excelent de divertisment și o dovadă de concept (PoC) pentru integrarea a multiple periferice (display SPI, touch SPI, modul audio UART, ADC pentru senzori) pe o singură placă de dezvoltare, totul optimizat pentru memoria limitată a unui sistem embedded.

## Utilizarea cunoștințelor din laboratoare

Pe parcursul dezvoltării acestui proiect, am aplicat practic conceptele studiate la curs și laboratoare astfel:

  * **Laboratorul 0: GPIO:** A fost fundația interacțiunii hardware. Am configurat pini ca output (prin ''pinMode'') pentru a controla starea celor 3 LED-uri (Roșu, Galben, Verde) folosind ''digitalWrite()''. Am folosit pinii ca input, activând rezistența internă (''INPUT_PULLUP''), pentru a citi mecanic și stabil apăsările butonului fizic cu ''digitalRead()''.
  * **Laboratorul 1: UART:** Comunicarea cu modulul DFPlayer Mini se bazează exclusiv pe acest protocol asincron. Deoarece portul serial hardware nativ al Arduino a fost folosit pentru debugging, am aplicat teoria UART folosind librăria ''SoftwareSerial'' pe pinii digitali 2 și 3, generând semnalele RX/TX necesare trimiterii de comenzi audio (ex. play, setare volum).
  * **Laboratorul 2: Întreruperi:** Deși în codul de nivel înalt nu am definit rutine ISR explicite (''attachInterrupt''), mecanismul întreruperilor lucrează intens "sub capotă". Librăria ''SoftwareSerial'' folosește Pin Change Interrupts (PCINT) pentru a prinde biții UART fără a bloca placa, iar funcția ''millis()'' (esențială pentru temporizările mele non-blocante, debounce-ul butonului și animații) este guvernată de o întrerupere a Timerului 0 intern.
  * **Laboratorul 3: Timere. PWM:** Am utilizat semnale PWM (Pulse Width Modulation) pentru a controla dinamic intensitatea backlight-ului de la ecranul TFT. Apelând ''analogWrite()'' pe pinul 6, am variat factorul de umplere (duty cycle) proporțional cu nivelul de lumină ambientală, reducând consumul de curent pe întuneric și protejând ochii utilizatorului.
  * **Laboratorul 4: ADC:** Esențial pentru senzorul de lumină (Fotorezistor/LDR). Acesta creează un divizor de tensiune, iar convertorul analog-digital (ADC) transformă tensiunea (0-5V) într-o valoare discretă (0-1023) folosind ''analogRead(A0)''. Pentru a combate interferențele și fenomenul de "crosstalk" produs în multiplexorul ADC-ului la aprinderea bruscă a LED-urilor, am aplicat o tehnică inginerească de tip "dummy read" cu un scurt delay pentru stabilizarea condensatorului intern S&H (Sample and Hold).
  * **Laboratorul 5: SPI:** Protocolul central al proiectului pentru transportul unui volum mare de date. Ecranul TFT și Controllerul Touch (XPT2046) folosesc ambele magistrala hardware SPI (împărțind liniile MOSI, MISO și SCK). Am folosit teoria de la laborator pentru a le controla fără coliziuni, asignând fiecăruia un pin distinct de Chip Select (''TFT_CS'' și ''TOUCH_CS''). Viteza mare a SPI-ului permite randarea animațiilor de cădere a pieselor.

## Descriere generală

Arhitectura proiectului este centrată pe placa Arduino, care acționează ca unitate centrală de procesare. Arduino preia input-ul tactil de la utilizator, citește senzorul de lumină ambientală, calculează logica matricei de joc și mutările oponentului CPU, și trimite comenzi către ecranul TFT, LED-uri și modulul DFPlayer Mini. 

Pentru a susține consumul ridicat al ecranului și al difuzorului, sistemul folosește un circuit de management al puterii separat (Battery Shield), care preia energia de la un acumulator Li-Ion și livrează o tensiune stabilă.

**Schema Bloc a interacțiunilor:**
  * **Sursa de alimentare:** Oferă 5V constanți și un curent de până la 3A.
  * **Creierul (Arduino Uno):** Coordonează perifericele și rulează algoritmii de filtrare touch și inteligență artificială.
  * **Input (Touchscreen & LDR):** Modulul XPT2046 trimite coordonatele atingerilor prin SPI. Fotorezistorul (LDR) trimite semnale analogice pentru reglarea automată a luminii (PWM). Mai există un buton fizic pentru declanșarea unor efecte sonore speciale.
  * **Output Vizual (Display TFT & LED-uri):** Ecranul comunică prin SPI pentru a randa interfața. 3 LED-uri (Roșu, Galben, Verde) se aprind pentru a evalua calitatea mutării jucătorului (Greșeală, Neutru, Mutare Excelentă).
  * **Output Audio (DFPlayer Mini + Difuzor 3W):** Comunică cu Arduino via UART (Serial). Citește efectele sonore de pe cardul MicroSD și le amplifică direct în difuzor.
![Block Diagram](Images/Connect_4_block_diagram.png)

## Hardware Design

**Lista de piese:**
  * 1 x Placă de dezvoltare (tip Arduino Uno)
  * 1 x Display TFT 2.4"/2.8" SPI cu controller Touchscreen XPT2046 integrat
  * 1 x Modul audio DFPlayer Mini MP3
  * 1 x Card MicroSD (max 32GB, formatat FAT32)
  * 1 x Difuzor Mini (3W, 8 Ohmi)
  * 1 x Fotorezistor (LDR) + 1 x Rezistență 10kOhm
  * 3 x LED-uri (Roșu, Galben, Verde) + 3 x Rezistențe 220 Ohm
  * 1 x Modul Powerbank (Battery Shield V3 18650, ieșire 5V/3A)
  * 1 x Acumulator Li-Ion 18650
  * 1 x Buton push (tactile switch)
  * 1 x Breadboard & Fire de conexiune (Dupont)
  
![Pins Layout](Images/Connect_4_pins_layout.png)

## Software Design

**Mediu de dezvoltare:** Proiectul a fost dezvoltat folosind mediul **Arduino IDE**.

**Librării utilizate:**
  * ''SPI.h'' - pentru comunicarea hardware cu display-ul și modulul touch.
  * ''Adafruit_GFX.h'' și ''Adafruit_ST7789.h'' - pentru randarea graficii (cercuri, meniuri, text).
  * ''XPT2046_Touchscreen.h'' - pentru preluarea datelor brute de la digitizer.
  * ''SoftwareSerial.h'' și ''DFRobotDFPlayerMini.h'' - pentru comunicarea cu playerul MP3.

**Algoritmi și Optimizări Relevante:**

  * **1. Stabilizarea ADC-ului și PWM (Filtru Crosstalk):** Pentru a evita "orbirea" senzorului LDR de fluctuațiile provocate de activarea LED-urilor adiacente, am implementat o dublă citire cu rol de descărcare a capacităților parazite (un "dummy read" urmat de citirea reală stabilizată pentru a mapa valoarea PWM pe TFT).
  * **2. Filtrarea Datelor de Touch (Medie Trunchiată):** Ecranele rezistive generează zgomot și date inconsistente. Am implementat o funcție care colectează un eșantion, îl sortează și elimină extremele (eliminarea a 20% valori zgomotoase pe axele X/Y) pentru precizie absolută.
  * **3. Inteligența Artificială (MiniMax Lite cu Euristici din MCTS):** Deoarece memoria RAM a plăcii (2KB) nu permite un arbore de căutare vast, am adaptat logica printr-un sistem euristic predictiv (Pattern Recognition). CPU-ul identifică tipare (ex. scanează ferestre de 4 poziții pentru: 3 piese proprii + 1 gol, specific Monte Carlo Tree Search) și evaluează pericole precum "furculițele".
  * **4. Evaluatorul Tactic (Feedback pe LED-uri):** Evaluează decizia umană simulând anticipat mutarea. Calculează câte amenințări s-au creat sau blocat și semnalizează hardware rezultatul (Roșu = Greșeală fatală, Galben = Mutare neutră, Verde = Blocaj reușit sau tactică ofensivă).
  
## Rezultate Obţinute

În urma implementării, a rezultat o consolă de joc complexă și foarte rapidă. 
Principalele realizări tehnice includ:
  * Optimizarea codului pentru a încăpea în limitările de Flash și RAM ale microcontrolerului, păstrând o "inteligență artificială" foarte capabilă (capabilă să te blocheze și să construiască "capcane" la dificultatea GREU).
  * Crearea unui algoritm de aliniere "magnetică" a atingerilor pe ecran pentru a compensa imprecizia tactilă de pe margini.
  * Stabilizarea consumului de curent, gestionând simultan ecranul, logica matematică și redarea audio, evitând zgomotul (crosstalk-ul) ADC-ului dintre senzorul LDR și LED-uri.

## Concluzii

Proiectul a demonstrat cât de important este atât managementul curentului, cât și gestiunea memoriei într-un sistem embedded. Cea mai mare provocare a fost adaptarea unui algoritm teoretic consumator de memorie (MCTS) într-un set de euristici statice rapide ce oferă aceeași provocare strategică, ocupând aproape 0 bytes de memorie dinamică adițională. Experiența este întregită de feedback-ul audio-vizual perfect sincronizat.

## Demo

[![Watch the demo](https://img.youtube.com/vi/oibC3gbSYUc/0.jpg)](https://www.youtube.com/watch?v=oibC3gbSYUc)

## Bibliografie / Resurse

**Resurse Hardware:**
  * Datasheet DFPlayer Mini / Cip Amplificator 8002
  * Specificații tehnice Acumulator 18650 Samsung
  * Datasheet XPT2046 Touch Controller
  * ATMega328P Datasheet (ADC multiplexer timings)

**Resurse Software:**
  * [Github DFRobot] DFPlayer Mini Library
  * [Adafruit] GFX & ST7789 Library Reference
  * Cursuri Inteligență Artificială: Algoritmi Minimax și MCTS aplicate în jocuri.
