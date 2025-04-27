#include <GL/glew.h>
#include <GL/glut.h>
#include <vector>
#include <iostream>
#include <cmath>
#include <chrono>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

std::chrono::high_resolution_clock::time_point lastFrameTime;
double speed = 100.0; // pixels per second
#define PI 3.14159265358979323846
#define WIDTH 1200
#define HEIGHT 600
#define raysN 15000
#define objectsN 3
#define maxBounces 3

struct Vertex {
    float x, y;
    float r, g, b;
};

struct Circle {
    double x, y;
    double r;
};

struct Ray {
    double xStart, yStart;
    double angle;
};

GLuint shaderProgram;
GLuint rayVAO, rayVBO;
GLuint circleVAO, circleVBO;

std::vector<Vertex> rayVertices;
std::vector<Vertex> circleVertices;

Circle objects[objectsN];
Ray rays[raysN];

double circleX = 300, circleY = 300, circleR = 40;
bool dragging = false;

Circle shadow = { 700, 300, 80 };
Circle shadow2 = { 900, 525, 40 };
Circle shadow3 = { 900, 120, 40 };


const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 ourColor;
void main()
{
    gl_Position = vec4((aPos.x / 600.0 - 1.0), (aPos.y / 300.0 - 1.0), 0.0, 1.0);
    ourColor = aColor;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 ourColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(ourColor, 1.0);
}
)";



double getCPUUsage()
{
    static ULARGE_INTEGER lastIdleTime = { 0 };
    static ULARGE_INTEGER lastKernelTime = { 0 };
    static ULARGE_INTEGER lastUserTime = { 0 };

    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
        return -1.0; // error

    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    ULONGLONG sysIdleDiff = idle.QuadPart - lastIdleTime.QuadPart;
    ULONGLONG sysKernelDiff = kernel.QuadPart - lastKernelTime.QuadPart;
    ULONGLONG sysUserDiff = user.QuadPart - lastUserTime.QuadPart;
    ULONGLONG sysTotal = sysKernelDiff + sysUserDiff;

    lastIdleTime = idle;
    lastKernelTime = kernel;
    lastUserTime = user;

    if (sysTotal == 0)
        return 0.0;

    return (1.0 - (double(sysIdleDiff) / double(sysTotal))) * 100.0;
}


void displayMetrics(int rayCount, int vertexCount, double deltaTimeMs)
{
    static int frameCounter = 0;
    static double timeAccumulator = 0.0;

    timeAccumulator += deltaTimeMs;
    frameCounter++;

    if (timeAccumulator >= 1000.0) // Every ~1 second
    {
        double averageFrameTime = timeAccumulator / frameCounter;
        double fps = 1000.0 / averageFrameTime;

        double cpuUsage = getCPUUsage();

        std::cout << "=====================================" << std::endl;
        std::cout << "Rays: " << rayCount << std::endl;
        std::cout << "Vertices: " << vertexCount << std::endl;
        std::cout << "Average Frame Time: " << averageFrameTime << " ms" << std::endl;
        std::cout << "FPS: " << fps << std::endl;
        std::cout << "CPU Usage: " << cpuUsage << " %" << std::endl;
        std::cout << "=====================================" << std::endl;

        frameCounter = 0;
        timeAccumulator = 0.0;
    }
}


void compileShaders()
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void generateRay(Circle circle, Ray rays[raysN])
{
    for (int i = 0; i < raysN; i++)
    {
        double angle = ((double)i / raysN) * 2 * PI;
        rays[i].xStart = circle.x;
        rays[i].yStart = circle.y;
        rays[i].angle = angle;
    }
}

bool intersectCircle(double ox, double oy, double dx, double dy, Circle c, double& tHit, double& nx, double& ny)
{
    double cx = c.x;
    double cy = c.y;
    double radius = c.r;

    double ocx = ox - cx;
    double ocy = oy - cy;

    double a = dx * dx + dy * dy;
    double b = 2.0 * (ocx * dx + ocy * dy);
    double c_val = ocx * ocx + ocy * ocy - radius * radius;

    double discriminant = b * b - 4.0 * a * c_val;
    if (discriminant < 0.0)
        return false;

    double sqrtDisc = sqrt(discriminant);
    double t0 = (-b - sqrtDisc) / (2.0 * a);
    double t1 = (-b + sqrtDisc) / (2.0 * a);

    double t = (t0 > 0.001) ? t0 : ((t1 > 0.001) ? t1 : -1.0);
    if (t < 0.0)
        return false;

    tHit = t;
    double hitX = ox + dx * t;
    double hitY = oy + dy * t;

    double len = sqrt((hitX - cx) * (hitX - cx) + (hitY - cy) * (hitY - cy));
    nx = (hitX - cx) / len;
    ny = (hitY - cy) / len;

    return true;
}


void drawRay(Ray rays[raysN], Circle objects[])
{
    for (int i = 0; i < raysN; i++)
    {
        double x = rays[i].xStart;
        double y = rays[i].yStart;
        double dx = cos(rays[i].angle);
        double dy = sin(rays[i].angle);

        float r = 1.0f, g = 0.8f, b = 0.2f;

        for (int bounce = 0; bounce <= maxBounces; bounce++)
        {
            double closestT = 1e9;
            double nx = 0.0, ny = 0.0;
            bool hit = false;

            // Find closest intersection
            for (int j = 0; j < objectsN; j++)
            {
                double t, tempNx, tempNy;
                if (intersectCircle(x, y, dx, dy, objects[j], t, tempNx, tempNy))
                {
                    if (t < closestT)
                    {
                        closestT = t;
                        nx = tempNx;
                        ny = tempNy;
                        hit = true;
                    }
                }
            }

            // Check intersection with screen borders
            double tx = (dx > 0) ? (WIDTH - x) / dx : (dx < 0) ? -x / dx : 1e9;
            double ty = (dy > 0) ? (HEIGHT - y) / dy : (dy < 0) ? -y / dy : 1e9;
            double borderT = std::min(tx, ty);

            if (borderT < closestT)
            {
                // Hit border
                double endX = x + dx * borderT;
                double endY = y + dy * borderT;
                rayVertices.push_back({ (float)x, (float)y, r, g, b });
                rayVertices.push_back({ (float)endX, (float)endY, r, g, b });
                break; // End
            }

            // Hit circle
            double hitX = x + dx * closestT;
            double hitY = y + dy * closestT;

            rayVertices.push_back({ (float)x, (float)y, r, g, b });
            rayVertices.push_back({ (float)hitX, (float)hitY, r, g, b });

            // Reflect
            double dot = dx * nx + dy * ny;
            dx = dx - 2 * dot * nx;
            dy = dy - 2 * dot * ny;

            x = hitX + dx * 0.1;
            y = hitY + dy * 0.1;

            // Set color depending on bounce number
            if (bounce == 0)
            {
                r = 1.0f; g = 1.0f; b = 1.0f; 
            }
            else if (bounce == 1)
            {
                r = 1; g = 0.5f; b = 0; 
            }
            else if (bounce == 2)
            {
                r = 0.7f; g = 0.4f; b = 1.0f; 
            }
            else
            {
                r = 0.5f; g = 0.5f; b = 0.5f; 
            }
        }
    }
}




void drawCircle(Circle circle, float r, float g, float b)
{
    int numSegments = 100;
    for (int i = 0; i < numSegments; i++)
    {
        float theta1 = (2.0f * PI * i) / numSegments;
        float theta2 = (2.0f * PI * (i + 1)) / numSegments;

        float x1 = circle.x + circle.r * cos(theta1);
        float y1 = circle.y + circle.r * sin(theta1);
        float x2 = circle.x + circle.r * cos(theta2);
        float y2 = circle.y + circle.r * sin(theta2);

        circleVertices.push_back({ x1, y1, r, g, b });
        circleVertices.push_back({ x2, y2, r, g, b });
    }
}

void updateVBO()
{
    rayVertices.clear();
    circleVertices.clear();

    Circle light = { circleX, circleY, circleR };
    objects[0] = shadow;
    objects[1] = shadow2;
    objects[2] = shadow3;

    generateRay(light, rays);
    drawRay(rays, objects);

    // Draw the circles
    drawCircle(light, 1.0f, 1.0f, 1.0f);
    drawCircle(shadow, 0.8f, 0.1f, 0.1f);
    drawCircle(shadow2, 0.1f, 0.8f, 0.1f);
    drawCircle(shadow3, 0.5, 0.2, 0.8);

    glBindVertexArray(rayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
    glBufferData(GL_ARRAY_BUFFER, rayVertices.size() * sizeof(Vertex), rayVertices.data(), GL_DYNAMIC_DRAW);

    glBindVertexArray(circleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, circleVBO);
    glBufferData(GL_ARRAY_BUFFER, circleVertices.size() * sizeof(Vertex), circleVertices.data(), GL_DYNAMIC_DRAW);
}

void setup()
{
    glewInit();
    compileShaders();

    glGenVertexArrays(1, &rayVAO);
    glGenBuffers(1, &rayVBO);
    glGenVertexArrays(1, &circleVAO);
    glGenBuffers(1, &circleVBO);

    glBindVertexArray(rayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(circleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, circleVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    updateVBO();
}

void display()
{
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    double deltaTime = std::chrono::duration<double, std::milli>(now - lastTime).count();
    lastTime = now;

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderProgram);

    glBindVertexArray(circleVAO);
    glDrawArrays(GL_LINES, 0, circleVertices.size());

    glBindVertexArray(rayVAO);
    glDrawArrays(GL_LINES, 0, rayVertices.size());

    displayMetrics(raysN, rayVertices.size(), deltaTime);

    glFlush();
}


void idle()
{
    using namespace std::chrono;

    static auto lastTime = high_resolution_clock::now();
    auto now = high_resolution_clock::now();
    double deltaTime = duration<double, std::milli>(now - lastTime).count();
    lastTime = now;

    // Move circle
    shadow.y += speed * (deltaTime / 1000.0); // IMPORTANT: deltaTime was in milliseconds

    if (shadow.y - shadow.r <= 0 || shadow.y + shadow.r >= HEIGHT)
    {
        speed = -speed;
    }

    updateVBO();
    glutPostRedisplay();
}



void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
    {
        double glX = (double)x;
        double glY = (double)(HEIGHT - y);

        double dx = glX - circleX;
        double dy = glY - circleY;
        if (dx * dx + dy * dy <= circleR * circleR)
            dragging = true;
    }
    else if (button == GLUT_LEFT_BUTTON && state == GLUT_UP)
        dragging = false;
}

void motion(int x, int y)
{
    if (dragging)
    {
        circleX = (double)x;
        circleY = (double)(HEIGHT - y);
        updateVBO();
    }
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
    glutInitWindowSize(WIDTH, HEIGHT);
    glutCreateWindow("Raytracing with GPU (Full Dynamic + Circles + Bounces)");

    setup();

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    lastFrameTime = std::chrono::high_resolution_clock::now();

    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);

    std::cout << "GPU Vendor: " << vendor << std::endl;
    std::cout << "GPU Renderer: " << renderer << std::endl;

    glutMainLoop();
    return 0;
}
