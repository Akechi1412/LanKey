#include "app/App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previous*/, PWSTR /*commandLine*/,
                    int /*showCommand*/) {
    lankey::app::App app;
    return app.run(instance);
}
