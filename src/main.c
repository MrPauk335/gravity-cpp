#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// --- КОНСТАНТЫ ---
const float G = 500.0f;
const float BASE_DT = 0.0005f; 
const int SUBSTEPS = 8;
const int GRID_SIZE = 50;
const float GRID_SPACING = 4.0f;

// --- СТРУКТУРЫ ---
struct Body {
    Vector3 position;
    Vector3 velocity;
    float mass;
    float radius;
    Color color;
    bool isFixed;
};

struct PlanetBuilder {
    bool active;
    Vector3 startPos; 
    Vector3 endPos;   
};

// --- ФИЗИКА И МАТЕМАТИКА ---
float GetSpacetimeCurve(float x, float z, const std::vector<Body>& bodies) {
    float y = -15.0f;
    for (const auto& b : bodies) {
        if (b.mass < 50.0f) continue;
        float distSq = Vector2LengthSqr(Vector2Subtract({x, z}, {b.position.x, b.position.z}));
        float depression = (b.mass * 0.5f) / (distSq + 60.0f);
        if (depression > 40.0f) depression = 40.0f;
        y -= depression;
    }
    return y;
}

Vector3 GetMouseOnPlane(Camera3D camera) {
    Ray ray = GetMouseRay(GetMousePosition(), camera);
    // Защита от деления на ноль, если камера параллельна горизонту
    if (fabs(ray.direction.y) < 0.001f) return {0,0,0}; 
    
    float t = -ray.position.y / ray.direction.y;
    if (t < 0) return {0,0,0}; 
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

// --- UI ПОМОЩНИКИ ---
// Функция рисует кнопку и возвращает true, если её нажали
bool GuiButton(Rectangle rect, const char* text, Color color) {
    bool pressed = false;
    // Рисуем
    DrawRectangleRec(rect, Fade(color, 0.8f));
    DrawRectangleLinesEx(rect, 2, WHITE);
    
    // Центрируем текст
    int fontSize = 30; // Крупный текст для телефона
    int textWidth = MeasureText(text, fontSize);
    DrawText(text, rect.x + (rect.width - textWidth)/2, rect.y + (rect.height - fontSize)/2, fontSize, WHITE);

    // Проверяем нажатие (Touch/Mouse)
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vector2 touch = GetMousePosition();
        if (CheckCollisionPointRec(touch, rect)) {
            pressed = true;
        }
    }
    return pressed;
}

int main() {
    // ВАЖНО: 0,0 означает полный экран на Android/Termux
    InitWindow(0, 0, "Gravity Mobile");
    SetTargetFPS(60);

    // Определяем размеры экрана
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 150.0f, 120.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    std::vector<Body> bodies;
    
    // Стартовый пресет
    bodies.push_back({ {0,0,0}, {0,0,0}, 5000.0f, 10.0f, GOLD, true });
    bodies.push_back({ {50,0,0}, {0,0,310.0f}, 100.0f, 3.0f, SKYBLUE, false });

    // Состояние приложения
    bool is2D = false;
    bool isPaused = false;
    bool isCreateMode = false; // false = вращать камеру, true = создавать планеты
    
    float timeSpeed = 1.0f;
    float newPlanetMass = 200.0f; // Текущая выбранная масса
    
    int cameraTarget = -1; // -1 = центр
    
    PlanetBuilder builder = { false, {0,0,0}, {0,0,0} };

    while (!WindowShouldClose()) {
        // Обновляем размеры, если экран повернули
        screenW = GetScreenWidth();
        screenH = GetScreenHeight();

        // --- ЛОГИКА КАМЕРЫ ---
        if (cameraTarget != -1 && cameraTarget < (int)bodies.size()) {
            camera.target = Vector3Lerp(camera.target, bodies[cameraTarget].position, 0.1f);
        } else {
            cameraTarget = -1;
            camera.target = Vector3Lerp(camera.target, {0,0,0}, 0.1f);
        }

        // Вращаем камеру ТОЛЬКО если мы не в режиме создания и не тыкаем в интерфейс
        // Простая проверка: если палец в центре экрана (не на кнопках)
        bool touchingUI = (GetMouseY() > screenH - 100) || (GetMouseY() < 80);
        
        if (!is2D && !isCreateMode && !touchingUI) {
            UpdateCamera(&camera, CAMERA_ORBITAL);
        }
        
        // --- ЛОГИКА СОЗДАНИЯ (ТОЛЬКО В РЕЖИМЕ CREATE) ---
        Vector3 mousePos3D = GetMouseOnPlane(camera);
        
        if (isCreateMode && !touchingUI) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                builder.active = true;
                builder.startPos = mousePos3D;
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                builder.endPos = mousePos3D;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && builder.active) {
                Vector3 diff = Vector3Subtract(builder.endPos, builder.startPos);
                // Если просто тапнули (без потягивания), не создаем
                if (Vector3Length(diff) > 2.0f) {
                    Vector3 velocity = Vector3Scale(diff, 5.0f);
                    float radius = sqrt(newPlanetMass) / 4.0f;
                    if (radius < 1.0f) radius = 1.0f;
                    
                    bodies.push_back({
                        builder.startPos,
                        velocity,
                        newPlanetMass,
                        radius,
                        (newPlanetMass > 1000) ? RED : WHITE,
                        false
                    });
                }
                builder.active = false;
            }
        }

        // --- ФИЗИКА ---
        if (!isPaused) {
            float dt = BASE_DT * timeSpeed;
            for (int step = 0; step < SUBSTEPS; step++) {
                for (size_t i = 0; i < bodies.size(); i++) {
                    if (bodies[i].isFixed) continue;
                    Vector3 totalForce = {0,0,0};
                    for (size_t j = 0; j < bodies.size(); j++) {
                        if (i==j) continue;
                        Vector3 diff = Vector3Subtract(bodies[j].position, bodies[i].position);
                        float distSq = Vector3LengthSqr(diff);
                        float dist = sqrt(distSq);
                        if (dist < bodies[i].radius + bodies[j].radius) dist = bodies[i].radius + bodies[j].radius;
                        float force = (G * bodies[i].mass * bodies[j].mass) / distSq;
                        totalForce = Vector3Add(totalForce, Vector3Scale(Vector3Normalize(diff), force));
                    }
                    bodies[i].velocity = Vector3Add(bodies[i].velocity, Vector3Scale(Vector3Scale(totalForce, 1.0f/bodies[i].mass), dt));
                }
                for (auto& b : bodies) {
                    if (!b.isFixed) b.position = Vector3Add(b.position, Vector3Scale(b.velocity, dt));
                }
            }
        }

        // --- ОТРИСОВКА ---
        BeginDrawing();
        ClearBackground(GetColor(0x050510FF)); // Темно-синий космос

        BeginMode3D(camera);
            
            // Сетка
            int halfSize = GRID_SIZE / 2;
            for (int x = -halfSize; x < halfSize; x++) {
                for (int z = -halfSize; z < halfSize; z++) {
                    float x1 = x * GRID_SPACING; float z1 = z * GRID_SPACING;
                    float x2 = (x+1) * GRID_SPACING; float z2 = (z+1) * GRID_SPACING;
                    float y1 = is2D ? -10 : GetSpacetimeCurve(x1, z1, bodies);
                    float y2 = is2D ? -10 : GetSpacetimeCurve(x2, z1, bodies);
                    float y3 = is2D ? -10 : GetSpacetimeCurve(x1, z2, bodies);
                    
                    Color c = is2D ? DARKGRAY : Fade(SKYBLUE, 0.3f);
                    DrawLine3D({x1, y1, z1}, {x2, y2, z1}, c);
                    DrawLine3D({x1, y1, z1}, {x1, y3, z2}, c);
                }
            }

            // Тела
            for (const auto& b : bodies) DrawSphere(b.position, b.radius, b.color);

            // Линия прицеливания
            if (isCreateMode && builder.active) {
                DrawSphere(builder.startPos, sqrt(newPlanetMass)/4.0f, Fade(GREEN, 0.5f));
                DrawLine3D(builder.startPos, builder.endPos, YELLOW);
                DrawSphere(builder.endPos, 0.5f, YELLOW);
            }

        EndMode3D();

        // --- UI ИНТЕРФЕЙС (МОБИЛЬНЫЙ) ---
        
        // 1. ВЕРХНЯЯ ПАНЕЛЬ (Масса)
        DrawRectangle(0, 0, screenW, 60, Fade(BLACK, 0.6f));
        DrawText("MASS:", 20, 15, 30, WHITE);
        // Слайдер массы (просто прямоугольник)
        DrawRectangle(120, 15, screenW - 140, 30, DARKGRAY);
        float massPercent = (newPlanetMass - 10.0f) / (2000.0f - 10.0f);
        DrawRectangle(120, 15, (screenW - 140) * massPercent, 30, isCreateMode ? GREEN : GRAY);
        // Проверка нажатия на слайдер
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && GetMouseY() < 60 && GetMouseX() > 120) {
            float clickPct = (GetMouseX() - 120.0f) / (float)(screenW - 140);
            newPlanetMass = 10.0f + clickPct * 2000.0f;
        }
        DrawText(TextFormat("%d", (int)newPlanetMass), 130, 15, 30, BLACK);


        // 2. НИЖНЯЯ ПАНЕЛЬ (Кнопки)
        int btnH = 80;
        int btnY = screenH - btnH - 10;
        int btnW = screenW / 5; // 5 кнопок в ряд

        // Кнопка 1: Режим (View/Create)
        if (GuiButton({(float)btnW*0, (float)btnY, (float)btnW-5, (float)btnH}, isCreateMode ? "BUILD" : "VIEW", isCreateMode ? GREEN : BLUE)) {
            isCreateMode = !isCreateMode;
        }

        // Кнопка 2: 2D/3D
        if (GuiButton({(float)btnW*1, (float)btnY, (float)btnW-5, (float)btnH}, is2D ? "2D" : "3D", DARKPURPLE)) {
            is2D = !is2D;
            if (is2D) {
                camera.position = (Vector3){ 0.0f, 200.0f, 0.0f };
                camera.projection = CAMERA_ORTHOGRAPHIC;
                camera.fovy = 100.0f;
            } else {
                camera.position = (Vector3){ 0.0f, 150.0f, 120.0f };
                camera.projection = CAMERA_PERSPECTIVE;
                camera.fovy = 45.0f;
            }
        }

        // Кнопка 3: Пауза
        if (GuiButton({(float)btnW*2, (float)btnY, (float)btnW-5, (float)btnH}, isPaused ? "| |" : ">", ORANGE)) {
            isPaused = !isPaused;
        }

        // Кнопка 4: Сброс
        if (GuiButton({(float)btnW*3, (float)btnY, (float)btnW-5, (float)btnH}, "RST", RED)) {
            bodies.clear();
            bodies.push_back({ {0,0,0}, {0,0,0}, 5000.0f, 10.0f, GOLD, true });
            cameraTarget = -1;
        }

        // Кнопка 5: Камера
        if (GuiButton({(float)btnW*4, (float)btnY, (float)btnW-5, (float)btnH}, "CAM", GRAY)) {
            cameraTarget++;
            if (cameraTarget >= (int)bodies.size()) cameraTarget = -1;
        }

        // Управление скоростью (над кнопками)
        DrawText(TextFormat("Speed: %.1fx", timeSpeed), 20, btnY - 40, 20, YELLOW);
        if (GuiButton({(float)screenW - 120, (float)btnY - 50, 50, 40}, "-", DARKGRAY)) timeSpeed *= 0.8f;
        if (GuiButton({(float)screenW - 60, (float)btnY - 50, 50, 40}, "+", DARKGRAY)) timeSpeed *= 1.2f;

        DrawFPS(20, 80);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
