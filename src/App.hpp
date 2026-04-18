#pragma once

class App {
  protected:
    int myId;
  public:
    bool exit = false; 
    App(int id) : myId(id) {}
    virtual ~App() {} 
    virtual void start() = 0;
    
    // Returns the ID of the app it wants to run next. 
    // Return myId to keep running. Return APP_MENU_ID to go to menu.
    virtual int run(InputHandler& in) = 0; 
    virtual void onExit() {}
    virtual bool handleMenuButton() {return false;}
};