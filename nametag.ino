/*********************************************************************
  Custom code for decorative nametag. Advertises Bluetooth, and responds
  to Bluetooth commands to light up Noepixels. Supports rainbow mode,
  colors, and other stuff I feel like doing along the way.
*********************************************************************/
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <bluefruit.h>

/********** Item by Item Configurables **********/
char* ADVERTISING_NAME = "Zad Nametag";
uint8_t NEOPIXEL_PIN = 11;
uint8_t NUM_PIXELS = 23;
/************************************************/

#define MAXCOMPONENTS  4
#define NEOPIXEL_VERSION_STRING "Neopixel v2.0"
uint8_t *pixelBuffer = NULL;
uint8_t width = 0;
uint8_t height = 0;
uint8_t stride;
uint8_t componentsValue;
bool is400Hz = false;
uint8_t components = 3;     // only 3 and 4 are valid values

Adafruit_NeoPixel neopixel = Adafruit_NeoPixel();

// BLE Service
BLEDfu  bledfu;
BLEDis  bledis;
BLEUart bleuart;

// Rainbow Params
unsigned long rainbowPreviousMillis = 0;
unsigned long pixelsInterval = 10;
unsigned long firstPixelHue = 0;
bool isRainbowActive = true;

bool setupJustRan = false;

void setup()
{
  Serial.begin(115200);

  // Config Neopixels
  neopixel.begin();

  // Init Bluefruit
  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
  Bluefruit.setName(ADVERTISING_NAME);

  Bluefruit.Periph.setConnectCallback(connect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Configure and Start Device Information Service
  bledis.setManufacturer("Rigel Madraswalla Industries");
  bledis.setModel(ADVERTISING_NAME);
  bledis.begin();

  // Configure and start BLE UART service
  bleuart.begin();

  // Set up and start advertising
  startAdv();

  // Set up Neopixels to start by default
  defaultSetup();
}

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include bleuart 128-bit uuid
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
     - Enable auto advertising if disconnected
     - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     - Timeout for fast mode is 30 seconds
     - Start(timeout) with timeout = 0 will advertise forever (until connected)

     For recommended advertising interval
     https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);

  Serial.println("Please select the 'Neopixels' tab, click 'Connect' and have fun");
}

void loop()
{
  // Echo received data
  if ( Bluefruit.connected() && bleuart.notifyEnabled() )
  {
    int command = bleuart.read();

    switch (command) {
      case 'V': {   // Get Version
          commandVersion();
          break;
        }

      case 'S': {   // Setup dimensions, components, stride...
          commandSetup();
          break;
        }

      case 'C': {   // Clear with color
          commandClearColor();
          break;
        }

      case 'B': {   // Set Brightness
          commandSetBrightness();
          break;
        }

      case 'P': {   // Set Pixel
          commandSetPixel();
          break;
        }

      case 'R' : { // Initiate Rainbow
          commandSetRainbow();
          break;
        }

    }
  }
  update_rainbow_params();
}

void swapBuffers()
{
  if (isRainbowActive) return;
  uint8_t *base_addr = pixelBuffer;
  int pixelIndex = 0;
  for (int j = 0; j < height; j++)
  {
    for (int i = 0; i < width; i++) {
      if (components == 3) {
        neopixel.setPixelColor(pixelIndex, neopixel.Color(*base_addr, *(base_addr + 1), *(base_addr + 2)));
      }
      else {
        neopixel.setPixelColor(pixelIndex, neopixel.Color(*base_addr, *(base_addr + 1), *(base_addr + 2), *(base_addr + 3) ));
      }
      base_addr += components;
      pixelIndex++;
    }
    pixelIndex += stride - width;   // Move pixelIndex to the next row (take into account the stride)
  }
  neopixel.show();

}

void commandVersion() {
  Serial.println(F("Command: Version check"));
  sendResponse(NEOPIXEL_VERSION_STRING);
}

void commandSetup() {
  Serial.println(F("Command: Setup"));

  // In practice, nametag lights don't change size. So dump these.
  bleuart.read(); // width
  bleuart.read(); // height
  width = NUM_PIXELS;
  height = 1;
  stride = bleuart.read();
  componentsValue = bleuart.read();
  is400Hz = bleuart.read();

  neoPixelType pixelType;
  pixelType = componentsValue + (is400Hz ? NEO_KHZ400 : NEO_KHZ800);

  components = (componentsValue == NEO_RGB || componentsValue == NEO_RBG || componentsValue == NEO_GRB || componentsValue == NEO_GBR || componentsValue == NEO_BRG || componentsValue == NEO_BGR) ? 3 : 4;

  Serial.printf("\tsize: %dx%d\n", width, height);
  Serial.printf("\tstride: %d\n", stride);
  Serial.printf("\tpixelType %d\n", pixelType);
  Serial.printf("\tcomponents: %d\n", components);

  if (pixelBuffer != NULL) {
    delete[] pixelBuffer;
  }

  uint32_t size = width * height;
  pixelBuffer = new uint8_t[size * components];
  neopixel.updateLength(size);
  neopixel.updateType(pixelType);
  neopixel.setPin(NEOPIXEL_PIN);

  // Done
  sendResponse("OK");

  setupJustRan = true;
}

void defaultSetup() {
  neoPixelType pixelType;
  pixelType = NEO_GRB + (is400Hz ? NEO_KHZ400 : NEO_KHZ800);

  components = (componentsValue == NEO_RGB || componentsValue == NEO_RBG || componentsValue == NEO_GRB || componentsValue == NEO_GBR || componentsValue == NEO_BRG || componentsValue == NEO_BGR) ? 3 : 4;
  width = NUM_PIXELS;
  height = 1;
  Serial.println("Default Setup...");
  Serial.printf("\tsize: %dx%d\n", width, height);
  Serial.printf("\tstride: %d\n", stride);
  Serial.printf("\tpixelType %d\n", pixelType);
  Serial.printf("\tcomponents: %d\n", components);

  if (pixelBuffer != NULL) {
    delete[] pixelBuffer;
  }

  uint32_t size = width * height;
  pixelBuffer = new uint8_t[size * components];
  neopixel.updateLength(size);
  neopixel.updateType(pixelType);
  neopixel.setPin(NEOPIXEL_PIN);
  neopixel.setBrightness(200);

}

void commandSetBrightness() {
  Serial.println(F("Command: SetBrightness"));

  // Read value
  uint8_t brightness = bleuart.read();

  // Set brightness
  neopixel.setBrightness(brightness);

  // Refresh pixels
  swapBuffers();


  // Done
  sendResponse("OK");
}

void commandClearColor() {
  if (setupJustRan) {
    setupJustRan = false;
    // Done
    sendResponse("OK");
    return;
  }
  isRainbowActive = false;
  Serial.println(F("Command: ClearColor"));

  // Read color
  uint8_t color[MAXCOMPONENTS];
  for (int j = 0; j < components;) {
    if (bleuart.available()) {
      color[j] = bleuart.read();
      j++;
    }
  }

  // Set all leds to color
  int size = width * height;
  uint8_t *base_addr = pixelBuffer;
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < components; j++) {
      *base_addr = color[j];
      base_addr++;
    }
  }

  // Swap buffers
  Serial.println(F("ClearColor completed"));
  swapBuffers();


  if (components == 3) {
    Serial.printf("\tclear (%d, %d, %d)\n", color[0], color[1], color[2] );
  }
  else {
    Serial.printf("\tclear (%d, %d, %d, %d)\n", color[0], color[1], color[2], color[3] );
  }

  // Done
  sendResponse("OK");
}

void commandSetPixel() {
  isRainbowActive = false;
  Serial.println(F("Command: SetPixel"));

  // Read position
  uint8_t x = bleuart.read();
  uint8_t y = bleuart.read();

  // Read colors
  uint32_t pixelOffset = y * width + x;
  uint32_t pixelDataOffset = pixelOffset * components;
  uint8_t *base_addr = pixelBuffer + pixelDataOffset;
  for (int j = 0; j < components;) {
    if (bleuart.available()) {
      *base_addr = bleuart.read();
      base_addr++;
      j++;
    }
  }

  // Set colors
  uint32_t neopixelIndex = y * stride + x;
  uint8_t *pixelBufferPointer = pixelBuffer + pixelDataOffset;
  uint32_t color;
  if (components == 3) {
    color = neopixel.Color( *pixelBufferPointer, *(pixelBufferPointer + 1), *(pixelBufferPointer + 2) );
    Serial.printf("\tcolor (%d, %d, %d)\n", *pixelBufferPointer, *(pixelBufferPointer + 1), *(pixelBufferPointer + 2) );
  }
  else {
    color = neopixel.Color( *pixelBufferPointer, *(pixelBufferPointer + 1), *(pixelBufferPointer + 2), *(pixelBufferPointer + 3) );
    Serial.printf("\tcolor (%d, %d, %d, %d)\n", *pixelBufferPointer, *(pixelBufferPointer + 1), *(pixelBufferPointer + 2), *(pixelBufferPointer + 3) );
  }
  neopixel.setPixelColor(neopixelIndex, color);
  neopixel.show();

  // Done
  sendResponse("OK");
}

void commandSetRainbow() {
  Serial.println(F("Taste the rainbow..."));
  if (neopixel.getBrightness() == 0) {
    neopixel.setBrightness(50);
  }
  isRainbowActive = true;

  // Done
  sendResponse("OK");
}

void sendResponse(char const *response) {
  Serial.printf("Send Response: %s\n", response);
  bleuart.write(response, strlen(response)*sizeof(char));
}

/*** Rigel's Methods ***/

void update_rainbow_params() {
  if ((unsigned long)(millis() - rainbowPreviousMillis) >= pixelsInterval) {
    rainbowPreviousMillis = millis();
    firstPixelHue += 256;
    if (firstPixelHue >= 5 * 65536) {
      firstPixelHue = 0;
    }
    rainbow(firstPixelHue);
  }
}

// Rainbow cycle along whole strip.
void rainbow(long firstPixelHue) {
  if (!isRainbowActive) return;
  for (int i = 0; i < neopixel.numPixels(); i++) { // For each pixel in strip...
    // Offset pixel hue by an amount to make one full revolution of the
    // color wheel (range of 65536) along the length of the strip
    // (strip.numPixels() steps):
    int pixelHue = firstPixelHue + (i * 65536L / neopixel.numPixels());
    // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
    // optionally add saturation and value (brightness) (each 0 to 255).
    // Here we're using just the single-argument hue variant. The result
    // is passed through strip.gamma32() to provide 'truer' colors
    // before assigning to each pixel:
    neopixel.setPixelColor(i, neopixel.gamma32(neopixel.ColorHSV(pixelHue)));
  }
  neopixel.show(); // Update strip with new contents

}
