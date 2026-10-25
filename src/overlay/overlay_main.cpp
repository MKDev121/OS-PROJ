#include "overlay.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

static void glfw_error_callback(int error, const char* description) {
    // Wayland compositor delegates window placement to compositor; ignore non-fatal positioning notice
    if (error == GLFW_FEATURE_UNAVAILABLE) {
        return;
    }
    std::cerr << "[GLFW Error " << error << "]: " << description << "\n";
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -h, --help               Show this help message\n"
              << "  -s, --status-file <path> Path to protector.status (default: /tmp/protector.status)\n"
              << "  -p, --pidfile <path>     Path to protector.pid (default: /tmp/protector.pid)\n"
              << "  -m, --mock               Launch in mock/demo simulation mode\n"
              << "  -e, --expanded           Start initially in expanded dashboard view\n"
              << "  --show-always            Always keep overlay HUD visible (disable auto-hide)\n"
              << "  --fps <limit>            Target framerate limit (default: 30)\n\n"
              << "Description:\n"
              << "  Interactive Dear ImGui overlay for Intelligent Session Protector.\n"
              << "  Automatically detects when video calls are joined, and provides\n"
              << "  interactive resource allocation percentages for video call,\n"
              << "  background workloads, and critical system processes.\n";
}

int main(int argc, char* argv[]) {
    std::string status_file = "/tmp/protector.status";
    std::string pidfile = "/tmp/protector.pid";
    bool mock_mode = false;
    bool start_expanded = false;
    bool show_always = false;
    int target_fps = 30;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--status-file") {
            if (i + 1 < argc) status_file = argv[++i];
        } else if (arg == "-p" || arg == "--pidfile") {
            if (i + 1 < argc) pidfile = argv[++i];
        } else if (arg == "-m" || arg == "--mock") {
            mock_mode = true;
        } else if (arg == "-e" || arg == "--expanded") {
            start_expanded = true;
        } else if (arg == "--show-always" || arg == "--show") {
            show_always = true;
        } else if (arg == "--fps") {
            if (i + 1 < argc) target_fps = std::max(5, std::stoi(argv[++i]));
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    glfwSetErrorCallback(glfw_error_callback);

    // On Linux systems running Wayland/X11, prefer X11/XWayland platform if DISPLAY is available
    // to ensure full compositor support for interactive window dragging, exact positioning, and floating.
    const char* display_env = std::getenv("DISPLAY");
    if (display_env && display_env[0] != '\0') {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }

    if (!glfwInit()) {
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        if (!glfwInit()) {
            std::cerr << "Error: Failed to initialize GLFW\n";
            return 1;
        }
    }

    bool auto_hide = !show_always && !mock_mode;
    isp::StatusReader initial_reader(pidfile, status_file);
    if (mock_mode) {
        initial_reader.set_mock_mode(true, "ACTIVE");
    }
    auto init_status = initial_reader.poll();
    bool initially_visible = !auto_hide || (init_status.state == "ACTIVE" || init_status.state == "IN_HYSTERESIS");

    // Configure window hints for floating translucent HUD
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, initially_visible ? GLFW_TRUE : GLFW_FALSE);

    int initial_w = start_expanded ? 540 : 490;
    int initial_h = start_expanded ? 520 : 62;

    GLFWwindow* window = glfwCreateWindow(initial_w, initial_h, "ISP Session Shield", nullptr, nullptr);
    if (!window) {
        std::cerr << "Warning: Could not create window with OpenGL 3.3 Core profile. Retrying without profile hint...\n";
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, initially_visible ? GLFW_TRUE : GLFW_FALSE);
        window = glfwCreateWindow(initial_w, initial_h, "ISP Session Shield", nullptr, nullptr);
        if (!window) {
            std::cerr << "Error: Failed to create GLFW window\n";
            glfwTerminate();
            return 1;
        }
    }

    // Position window nicely near top-right corner of primary monitor
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (primary) {
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        if (mode) {
            int pos_x = mode->width - initial_w - 40;
            int pos_y = 50;
            glfwSetWindowPos(window, pos_x, pos_y);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Stateless HUD, no unwanted imgui.ini clutter

    // Setup Dear ImGui backends
    const char* glsl_version = "#version 330";
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::cerr << "Error: ImGui GLFW init failed\n";
        return 1;
    }
    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        std::cerr << "Error: ImGui OpenGL3 init failed\n";
        return 1;
    }

    isp::StatusReader reader(pidfile, status_file);
    if (mock_mode) {
        reader.set_mock_mode(true, "ACTIVE");
        std::cout << "[ISP Overlay] Running in simulated mock mode.\n";
    }

    isp::OverlayRenderer renderer(window, reader);
    renderer.set_auto_hide_idle(auto_hide);
    if (start_expanded) {
        renderer.set_expanded(true);
    }

    std::cout << "[ISP Overlay] HUD initialized. Click '[+] Details' to inspect live resource allocations.\n";

    auto prev_time = std::chrono::steady_clock::now();
    const auto frame_duration = std::chrono::milliseconds(1000 / target_fps);

    while (!glfwWindowShouldClose(window) && !renderer.should_close()) {
        auto frame_start = std::chrono::steady_clock::now();
        float delta_time = std::chrono::duration<float>(frame_start - prev_time).count();
        prev_time = frame_start;

        glfwPollEvents();

        // Start Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render ISP HUD
        renderer.render(delta_time);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        // Frame rate limiter
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frame_start);
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "[ISP Overlay] Exited gracefully.\n";
    return 0;
}
