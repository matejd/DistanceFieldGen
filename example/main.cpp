#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <cassert>
#include <cmath>

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#include <GLFW/glfw3.h>
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <thread>
#endif

#define ASSERT(expr) assert(expr)
#define DEBUG
#define NORETURN __attribute__((noreturn))

namespace { // Unnamed namespace (everything except main is in it).

const char* k_windowTitle = "Distance Field Example";
const int k_windowWidth = 1280;
const int k_windowHeight = 720;

const float PI = static_cast<float>(M_PI);
const float TwoPI = 2.f * PI;

std::vector<uint8_t> readFile(const std::string& path);
GLuint uploadShader(const std::string& vsSource, const std::string& fsSource);

enum class AppState
{
    Idle,
    Rotating
};

// Emergency vector math!
struct Vec3
{
    float x, y, z;
    Vec3() = default;
    Vec3(const float x_, const float y_, const float z_): x(x_), y(y_), z(z_) {}
    Vec3 operator + (const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator - (const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator - () const { return Vec3(-x, -y, -z); }
    Vec3 normalized() const {
        const float length = std::sqrt(x*x + y*y + z*z);
        return Vec3(x / length, y / length, z / length);
    }
    Vec3 cross(const Vec3& b) const {
        return Vec3(y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x);
    }
};

struct OrbitalCamera
{
    float radius;
    float theta;
    float phi;
};

int fbWidth, fbHeight;
bool windowFocused = true;
GLuint distanceFieldShader;
GLuint distanceFieldTex;
GLuint fullVertexBuffer;
GLint canvasSizeLoc;
GLint rightLoc, forwardLoc, upLoc, posLoc;
GLint distFieldSamLoc;
double mouseStartX, mouseStartY;
OrbitalCamera orbiCam;
AppState appState = AppState::Idle;

bool setup(GLFWwindow* window)
{
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    std::cout << "Framebuffer size: " << fbWidth << "x" << fbHeight << std::endl;

    orbiCam.radius = 1.5f;
    orbiCam.phi = -2.8f;
    orbiCam.theta = 1.39f;

    distanceFieldShader = uploadShader(std::string() +
        "attribute vec4 posNDC;    \n"
        "void main() {             \n"
        "    gl_Position = posNDC; \n"
        "}",
        std::string() +
        "uniform sampler2D distFieldSam; \n"
        "const float distFieldSize = 32.0;   \n"
        "uniform vec2 canvasSize;  \n"
        "uniform vec3 forward;     \n"
        "uniform vec3 up;          \n"
        "uniform vec3 right;       \n"
        "uniform vec3 pos;         \n"
        "float sampleDistanceField(vec3 position) { \n"
        "   vec3 coords = clamp(position, 0.0, 1.0); \n"
        "   vec2 unpacked; \n"
        "   unpacked.y = coords.y; \n"
        "   unpacked.x = (floor(coords.z*distFieldSize) + coords.x) / distFieldSize; \n"
        "   float d1 = texture2D(distFieldSam, unpacked).r; \n"
        "   unpacked.x = (floor(coords.z*distFieldSize) + 1.0 + coords.x) / distFieldSize; \n"
        "   float d2 = texture2D(distFieldSam, unpacked).r; \n"
        "   float a = fract(coords.z*distFieldSize); \n"
        "   return sqrt(dot(position-coords, position-coords)) + mix(d1, d2, a); \n"
        "}\n"
        "float march(vec3 origin, vec3 dir) {                         \n"
        "    float t = 0.0;                                           \n"
        "    for (int i = 0; i < 10; i++) {                           \n"
        "        float h = sampleDistanceField(origin + t*dir);       \n"
        "        t += h;                                              \n"
        "    }                                                        \n"
        "    return t;                                                \n"
        "}                                                            \n"
        "void main() {             \n"
        "    float ratio = canvasSize.y / canvasSize.x;               \n"
        "    float halfWidth = canvasSize.x / 2.f;                    \n"
        "    float halfHeight = canvasSize.y / 2.f;                   \n"
        "    float x = (gl_FragCoord.x - halfWidth)  / canvasSize.x;  \n"
        "    float y = ratio * (gl_FragCoord.y - halfHeight) / canvasSize.y; \n"
        "    vec3 dir = normalize(forward + x*right + y*up);          \n"
        "    float dist = march(pos, dir);                            \n"
        "    gl_FragColor.g = dist; \n"
        "}");
    canvasSizeLoc = glGetUniformLocation(distanceFieldShader, "canvasSize");
    forwardLoc    = glGetUniformLocation(distanceFieldShader, "forward");
    upLoc         = glGetUniformLocation(distanceFieldShader, "up");
    rightLoc      = glGetUniformLocation(distanceFieldShader, "right");
    posLoc        = glGetUniformLocation(distanceFieldShader, "pos");
    distFieldSamLoc = glGetUniformLocation(distanceFieldShader, "distFieldSam");

    //ASSERT(canvasSizeLoc != -1 && forwardLoc != -1 && upLoc != -1 && rightLoc != -1 && posLoc != -1);

    // Two triangles in normalized device coordinates, covering the entire framebuffer.
    float fullVertices[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 0.f, 1.f,
         1.f,  1.f, 0.f, 1.f,

        -1.f, -1.f, 0.f, 1.f,
         1.f,  1.f, 0.f, 1.f,
        -1.f,  1.f, 0.f, 1.f,
    };
    glGenBuffers(1, &fullVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, fullVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fullVertices), fullVertices, GL_STATIC_DRAW);

    std::vector<uint8_t> distanceField = readFile("armadillo_dist.bin");
    std::cout << "Read " << distanceField.size() << " bytes from distance field data file." << std::endl;

    glGenTextures(1, &distanceFieldTex);
    glBindTexture(GL_TEXTURE_2D, distanceFieldTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 32*32, 32, 0, GL_RED, GL_UNSIGNED_BYTE, distanceField.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return true;
}

void drawFrame()
{
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(distanceFieldShader);

    const Vec3 pos(orbiCam.radius * std::sin(orbiCam.theta) * std::cos(orbiCam.phi) + 0.5f,
                   orbiCam.radius * std::sin(orbiCam.theta) * std::sin(orbiCam.phi) + 0.5f,
                   orbiCam.radius * std::cos(orbiCam.theta)                         + 0.5f);
    const Vec3 forward = (Vec3(0.5f, 0.5f, 0.5f)-pos).normalized();
    const float halfPi = PI/2.f;
    const Vec3 right   = Vec3(std::cos(orbiCam.phi - halfPi),
                              std::sin(orbiCam.phi - halfPi),
                              0.f);
    const Vec3 up      = right.cross(forward);

    glUniform2f(canvasSizeLoc, fbWidth, fbHeight);
    glUniform3f(forwardLoc, forward.x, forward.y, forward.z);
    glUniform3f(upLoc,      up.x,      up.y,      up.z);
    glUniform3f(rightLoc,   right.x,   right.y,   right.z);
    glUniform3f(posLoc,     pos.x,     pos.y,     pos.z);

    glActiveTexture(GL_TEXTURE0+0);
    glBindTexture(GL_TEXTURE_2D, distanceFieldTex);
    glUniform1i(distFieldSamLoc, 0);

    glBindBuffer(GL_ARRAY_BUFFER, fullVertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), reinterpret_cast<GLvoid*>(0));
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

float clamp(const float v, const float min, const float max)
{
    if (v < min)
        return min;
    if (v > max)
        return max;
    return v;
}

void onCursorPos(GLFWwindow*, double x, double y)
{
    if (appState == AppState::Rotating) {
        const float dx = static_cast<float>(x-mouseStartX);
        const float dy = static_cast<float>(y-mouseStartY);
        mouseStartX = x;
        mouseStartY = y;
        orbiCam.phi   -= (dx / fbWidth)  * TwoPI;
        orbiCam.theta -= (dy / fbHeight) * PI;
        orbiCam.theta  = clamp(orbiCam.theta, 0.001f, PI-0.001f);
        return;
    }
}

void onMouseButton(GLFWwindow* window, int button, int action, int /*mods*/)
{
    if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
        appState = AppState::Rotating;
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        mouseStartX = x;
        mouseStartY = y;
        return;
    }

    appState = AppState::Idle;
}

void onScroll(GLFWwindow*, double /*xoffset*/, double /*yoffset*/)
{
}

void onKey(GLFWwindow*, int /*key*/, int /*scancode*/, int /*action*/, int /*mods*/)
{
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::vector<uint8_t> contents;
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        std::cout << "Failed to open " << path << "!" << std::endl;
        ASSERT(false);
        return contents;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff numBytes = in.tellg();
    if (numBytes <= 0) {
        std::cout << "Opened, but failed to read " << path << "!" << std::endl;
        ASSERT(false);
        return contents;
    }
    contents.resize(static_cast<size_t>(numBytes)); // std::streamoff is signed!
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(&contents[0]), numBytes);
    in.close();
    return contents;
}

GLuint uploadShader(const std::string& vsSource, const std::string& fsSource)
{
    ASSERT(fsSource.size() > 0);
    const std::string fsHeader = std::string("#if GL_ES\n") +
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" +
        "precision highp float;\n" +
        "#else\n" +
        "precision mediump float;\n" +
        "#endif\n" +
        "#endif\n";
    const std::string fsSourceFinal = fsHeader + fsSource;
    const std::string vsSourceFinal = vsSource;
    const GLenum types[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    GLuint ids[2];
    for (int i = 0; i < 2; i++) {
        ids[i] = glCreateShader(types[i]);
        const char* ptr = (i == 0) ? &vsSourceFinal[0] : &fsSourceFinal[0];
        glShaderSource(ids[i], 1, &ptr, nullptr);
        glCompileShader(ids[i]);
        GLint status = 0;
        glGetShaderiv(ids[i], GL_COMPILE_STATUS, &status);
        if (!status) {
            GLchar info[1024];
            GLsizei length = 0;
            glGetShaderInfoLog(ids[i], sizeof(info), &length, info);
            std::cout << "Failed to compile:" << std::endl << info << std::endl;
            ASSERT(false);
            return std::numeric_limits<GLuint>::max();
        }
    }

    std::vector<std::string> attributes;
    for (int i = 0; i < 2; i++) {
        std::stringstream ss;
        if (i == 0)
            ss << vsSourceFinal;
        else
            ss << fsSourceFinal;
        ss.seekp(0);
        std::string token;
        ss >> token;
        while (token != "main" && !ss.eof()) {
            if (token == "attribute") {
                std::string type, name;
                ss >> type >> name;
                name = name.substr(0, name.find_first_of("[ ;"));
                attributes.push_back(name);
            }

            ss >> token;
        }
    }

    GLuint programId = glCreateProgram();
    glAttachShader(programId, ids[0]);
    glAttachShader(programId, ids[1]);
    for (size_t i = 0; i < attributes.size(); i++) {
        glBindAttribLocation(programId, i, attributes[i].c_str());
    }
    glLinkProgram(programId);
    GLint linked = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &linked);
    ASSERT(linked);

    std::cout << "Shader " << programId << " uploaded." << std::endl;
    return programId;
}

void onFocus(GLFWwindow*, int focused)
{
    windowFocused = (focused > 0);
}

void glfwErrorCallback(int /*error*/, const char* description)
{
    std::cout << "GLFW error: " << description << std::endl;
}

void NORETURN openglDebugCallback(GLenum source, GLenum type, GLuint /*id*/, GLenum severity,
                                  GLsizei /*length*/, const GLchar* message, GLvoid* /*userParam*/)
{
    std::string sourceString;
    std::string typeString;
    std::string severityString;

    switch (source) {
        case GL_DEBUG_SOURCE_API:             sourceString = "API";             break;
        case GL_DEBUG_SOURCE_APPLICATION:     sourceString = "Application";     break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   sourceString = "Window System";   break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceString = "Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     sourceString = "Third Party";     break;
        case GL_DEBUG_SOURCE_OTHER:           sourceString = "Other";           break;
        default:                              sourceString = "Unknown";         break;
    }

    switch (type) {
        case GL_DEBUG_TYPE_ERROR:               typeString = "Error";               break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeString = "Deprecated Behavior"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeString = "Undefined Behavior";  break;
        case GL_DEBUG_TYPE_PORTABILITY_ARB:     typeString = "Portability";         break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typeString = "Performance";         break;
        case GL_DEBUG_TYPE_OTHER:               typeString = "Other";               break;
        default:                                typeString = "Unknown";             break;
    }
 
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:   severityString = "High";    break;
        case GL_DEBUG_SEVERITY_MEDIUM: severityString = "Medium";  break;
        case GL_DEBUG_SEVERITY_LOW:    severityString = "Low";     break;
        default:                       severityString = "Unknown"; break;
    }

    std::cout << "OpenGL callback ["
              << sourceString
              << "][" << typeString
              << "][" << severityString
              << "]: " << message;
    std::cout << std::endl;
    ASSERT(false);
}

} // Unnamed namespace.

#ifdef EMSCRIPTEN
void emscriptenCallback()
{
    drawFrame();
    glfwSwapBuffers(g_window);
    glfwPollEvents();
}

int main(int, char**)
{
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(k_windowWidth, k_windowHeight, k_windowTitle, nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window,         onKey);
    glfwSetCursorPosCallback(window,   onCursorPos);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetScrollCallback(window,      onScroll);

    setup(window);
    emscripten_set_main_loop(emscriptenCallback, 0, 1);

    // Never reached.
    return 0;
}

#else // EMSCRIPTEN

int main(int, char**)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GL_TRUE) {
        std::cout << "Failed to init GLFW!" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(k_windowWidth, k_windowHeight, k_windowTitle, nullptr, nullptr);
    if (!window) {
        std::cout << "Failed to create a GLFW window!" << std::endl;
        glfwTerminate();
        return 2;
    }

    glfwMakeContextCurrent(window);

    // Setup OpenGL extensions. glewExperimental = true tells glew *not* to use glGetString()
    // to query extensions (http://stackoverflow.com/a/20822876). We still need to
    // clear invalid enum error caused by glew (we call glGetError()).
    glewExperimental = true;
    GLenum glewError = glewInit();
    ASSERT(glewError == GLEW_OK);
    glGetError();
    ASSERT(glGetError() == GL_NO_ERROR);

#ifdef DEBUG
    // Enable ARB_debug_output. Report events immediately (synchronous).
    ASSERT(glDebugMessageCallback != nullptr);
    glDebugMessageCallback(openglDebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif

    // Core profile requires a VAO (http://github.prideout.net/modern-opengl-prezo/modern-opengl.pdf).
    // Creating one and only here.
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glfwSetKeyCallback(window,         onKey);
    glfwSetCursorPosCallback(window,   onCursorPos);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetScrollCallback(window,      onScroll);
    glfwSetWindowFocusCallback(window, onFocus);

    if (!setup(window)) {
        glfwTerminate();
        return 3;
    }

    while (true) {
        if (!windowFocused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        drawFrame();
        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) || glfwWindowShouldClose(window))
            break;
    }

    std::cout << "Terminating..." << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#endif
