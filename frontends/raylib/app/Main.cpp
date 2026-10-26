#include "app/App.h"
#include "app/LaunchOptions.h"

int main(int argumentCount, char** arguments) {
    App app;
    app.run(parseLaunchOptions(argumentCount, arguments));
    return 0;
}
