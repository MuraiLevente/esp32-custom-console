#pragma once

class InputHandler {
  private:
    int lastJoyBtn = HIGH, lastBtn1 = HIGH, lastBtn2 = HIGH;
    int lastBtn3 = HIGH, lastBtn4 = HIGH, lastBtn5 = HIGH;
    unsigned long lastTime = 0;
  public:
    int joyX, joyY;
    
    // "isDown" means currently held. "pressed" means it was just clicked this frame.
    bool joyBtnDown, joyBtnPressed;
    bool btn1Down, btn1Pressed; // UP
    bool btn2Down, btn2Pressed; // LEFT
    bool btn3Down, btn3Pressed; // DOWN
    bool btn4Down, btn4Pressed; // RIGHT
    bool btn5Down, btn5Pressed; // EXIT
    float deltaTime;

    void init() {
      pinMode(JOY_BTN_PIN, INPUT_PULLUP);
      pinMode(BTN1_PIN, INPUT_PULLUP);
      pinMode(BTN2_PIN, INPUT_PULLUP);
      pinMode(BTN3_PIN, INPUT_PULLUP);
      pinMode(BTN4_PIN, INPUT_PULLUP);
      pinMode(BTN5_PIN, INPUT_PULLUP);
      lastTime = millis(); // Initialize the timer
    }

    void update() {
      unsigned long currentTime = millis();
      deltaTime = (currentTime - lastTime) / 1000.0f; // Convert milliseconds to seconds
      lastTime = currentTime;
      joyX = analogRead(JOY_X_PIN);
      joyY = analogRead(JOY_Y_PIN);

      int currJoy = digitalRead(JOY_BTN_PIN);
      joyBtnDown = (currJoy == LOW);
      joyBtnPressed = (currJoy == LOW && lastJoyBtn == HIGH);
      lastJoyBtn = currJoy;

      int curr1 = digitalRead(BTN1_PIN);
      btn1Down = (curr1 == LOW);
      btn1Pressed = (curr1 == LOW && lastBtn1 == HIGH);
      lastBtn1 = curr1;

      int curr2 = digitalRead(BTN2_PIN);
      btn2Down = (curr2 == LOW);
      btn2Pressed = (curr2 == LOW && lastBtn2 == HIGH);
      lastBtn2 = curr2;

      int curr3 = digitalRead(BTN3_PIN);
      btn3Down = (curr3 == LOW);
      btn3Pressed = (curr3 == LOW && lastBtn3 == HIGH);
      lastBtn3 = curr3;

      int curr4 = digitalRead(BTN4_PIN);
      btn4Down = (curr4 == LOW);
      btn4Pressed = (curr4 == LOW && lastBtn4 == HIGH);
      lastBtn4 = curr4;

      int curr5 = digitalRead(BTN5_PIN);
      btn5Down = (curr5 == LOW);
      btn5Pressed = (curr5 == LOW && lastBtn5 == HIGH);
      lastBtn5 = curr5;
    }
};
