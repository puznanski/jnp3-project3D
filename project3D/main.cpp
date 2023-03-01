#include "app.h"

INT WINAPI wWinMain(_In_ [[maybe_unused]] HINSTANCE instance,
        _In_opt_ [[maybe_unused]] HINSTANCE prev_instance,
        _In_ [[maybe_unused]] PWSTR cmd_line,
        _In_ [[maybe_unused]] INT cmd_show) {
    App app(L"JNP3 - 3D Project");

    if (SUCCEEDED(app.Initialize(instance, cmd_show))) {
        app.RunMessageLoop();
    }

    return 0;
}
