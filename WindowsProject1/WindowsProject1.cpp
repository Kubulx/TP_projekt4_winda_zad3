#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <cmath>
#include <set>
#include <algorithm>
#include <iostream>

using namespace Gdiplus;
using namespace std;

#pragma comment(lib, "gdiplus.lib")

const int FLOOR_COUNT = 4;
const int FLOOR_HEIGHT = 100;
const int ELEVATOR_WIDTH = 140;
const int ELEVATOR_HEIGHT = 100;
const float MAX_WEIGHT = 600.0f;
const float WEIGHT_PER_PERSON = 70.0f;
const float ELEVATOR_SPEED = 2.5f;
const DWORD DOOR_OPEN_DURATION = 1500;
const int FULL_ELEVATOR_THRESHOLD = 6;
const DWORD IDLE_RETURN_DELAY = 5000;

const int PASSENGER_HEAD_RADIUS = 6;
const int PASSENGER_BODY_HEIGHT = 15;
const int PASSENGER_ARM_SPAN = 15;
const int PASSENGER_LEG_LENGTH = 15;
const int PASSENGER_TOTAL_HEIGHT = PASSENGER_HEAD_RADIUS * 2 + PASSENGER_BODY_HEIGHT + PASSENGER_LEG_LENGTH;
const int PASSENGER_TOTAL_WIDTH = PASSENGER_HEAD_RADIUS * 2;
const int PASSENGER_SPACING = 5;
const int UI_WALL_START_X = 100;
const int UI_WALL_WIDTH = 500;
const int UI_BUTTON_AREA_START_X = UI_WALL_START_X + UI_WALL_WIDTH + 20;
const int UI_BUTTON_WIDTH = 50;
const int UI_BUTTON_HEIGHT = 25;
const int UI_BUTTON_SPACING_X = 5;

enum class ElevatorState {
    IDLE,
    MOVING,
    STOPPED
};
enum class Direction {
    UP,
    DOWN,
    NONE
};
struct Passenger {
    int id;
    int startFloor;
    int targetFloor;
    bool inElevator = false;
    bool boarding = false;
    float animX = 0.0f;
    float animY = 0.0f;
    float boardingProgress = 0.0f;
    int posIndex = 0;
};

ElevatorState currentState = ElevatorState::IDLE;
Direction currentDirection = Direction::NONE;
float elevatorY = (FLOOR_COUNT - 1) * FLOOR_HEIGHT;
int targetFloor = -1;
DWORD stateTimer = 0;

ULONG_PTR gdiplusToken;
HWND hwndGlobal;
vector<Passenger> passengers;
int nextPassengerId = 0;
HDC memDC = nullptr;
HBITMAP memBitmap = nullptr;
int memWidth = 0, memHeight = 0;

int GetFloorFromY(float y) {
    return (int)round(y / FLOOR_HEIGHT);
}

void ReorganizePositions() {
    int elevatorPos = 0;
    for (auto& p : passengers) {
        if (p.inElevator) p.posIndex = elevatorPos++;
    }
    for (int i = 0; i < FLOOR_COUNT; ++i) {
        int floorPos = 0;
        for (auto& p : passengers) {
            if (!p.inElevator && p.startFloor == i) p.posIndex = floorPos++;
        }
    }
}

void DecideNextMove() {
    int currentFloor = GetFloorFromY(elevatorY);

    set<int> destinations;
    set<int> up_requests;
    set<int> down_requests;
    int passengers_inside = 0;

    for (const auto& p : passengers) {
        if (p.inElevator) {
            destinations.insert(p.targetFloor);
            passengers_inside++;
        }
        else {
            if (p.targetFloor < p.startFloor) up_requests.insert(p.startFloor);
            else down_requests.insert(p.startFloor);
        }
    }

    if (destinations.empty() && up_requests.empty() && down_requests.empty()) {
        if (currentState != ElevatorState::IDLE) {
            stateTimer = GetTickCount();
        }
        currentState = ElevatorState::IDLE;
        currentDirection = Direction::NONE;
        targetFloor = -1;
        return;
    }

    bool is_full = (passengers_inside >= FULL_ELEVATOR_THRESHOLD);
    if (currentDirection == Direction::NONE) {
        int closest_task = -1;
        int min_dist = FLOOR_COUNT;
        for (int floor : destinations) if (abs(floor - currentFloor) < min_dist) {
            min_dist = abs(floor - currentFloor); closest_task = floor;
        }
        if (!is_full) {
            for (int floor : up_requests) if (abs(floor - currentFloor) < min_dist) {
                min_dist = abs(floor - currentFloor);
                closest_task = floor;
            }
            for (int floor : down_requests) if (abs(floor - currentFloor) < min_dist) {
                min_dist = abs(floor - currentFloor);
                closest_task = floor;
            }
        }
        if (closest_task != -1) targetFloor = closest_task;
        else { currentState = ElevatorState::IDLE; return; }

        if (targetFloor == currentFloor) {
            currentState = ElevatorState::STOPPED;
            stateTimer = GetTickCount();
        }
        else {
            currentState = ElevatorState::MOVING;
            currentDirection = (targetFloor < currentFloor) ? Direction::UP : Direction::DOWN;
        }
        return;
    }

    for (int i = 0; i < 2; ++i) {
        int next_stop = -1;
        if (currentDirection == Direction::UP) {
            for (int f = currentFloor; f >= 0; --f) {
                if (destinations.count(f) || (!is_full && up_requests.count(f))) {
                    next_stop = f;
                    break;
                }
            }
        }
        else {
            for (int f = currentFloor; f < FLOOR_COUNT; ++f) {
                if (destinations.count(f) || (!is_full && down_requests.count(f))) {
                    next_stop = f;
                    break;
                }
            }
        }

        if (next_stop != -1) {
            targetFloor = next_stop;
            if (targetFloor == currentFloor) {
                currentState = ElevatorState::STOPPED;
                stateTimer = GetTickCount();
            }
            else {
                currentState = ElevatorState::MOVING;
            }
            return;
        }
        currentDirection = (currentDirection == Direction::UP) ? Direction::DOWN : Direction::UP;
    }

    currentState = ElevatorState::IDLE;
    currentDirection = Direction::NONE;
}

void MoveElevator() {
    const float BOARDING_SPEED = 4.0f;

    int elevatorX = UI_WALL_START_X + UI_WALL_WIDTH - 150;
    int elevatorScreenY = (int)elevatorY + FLOOR_HEIGHT - ELEVATOR_HEIGHT;
    for (auto& p : passengers) {
        if (p.boarding) {
            float targetX = elevatorX + (ELEVATOR_WIDTH - (FULL_ELEVATOR_THRESHOLD * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING))) / 2.0f + (p.posIndex * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING));
            float targetY = (float)(elevatorScreenY + ELEVATOR_HEIGHT - PASSENGER_TOTAL_HEIGHT);

            float dx = targetX - p.animX;
            float dy = targetY - p.animY;
            float dist = sqrt(dx * dx + dy * dy);
            if (dist < BOARDING_SPEED) {
                p.animX = targetX;
                p.animY = targetY;
                p.boardingProgress = 1.0f;
                p.boarding = false;
                p.inElevator = true;
                ReorganizePositions();
            }
            else {
                p.animX += BOARDING_SPEED * dx / dist;
                p.animY += BOARDING_SPEED * dy / dist;
            }
        }
    }
    switch (currentState) {
    case ElevatorState::IDLE: {
        set<int> all_requests;
        for (const auto& p : passengers) if (!p.inElevator) all_requests.insert(p.startFloor);

        if (!all_requests.empty()) {
            DecideNextMove();
        }
        else {
            int groundFloor = FLOOR_COUNT - 1;
            if (GetFloorFromY(elevatorY) != groundFloor && GetTickCount() - stateTimer > IDLE_RETURN_DELAY) {
                targetFloor = groundFloor;
                if (targetFloor != GetFloorFromY(elevatorY)) {
                    currentState = ElevatorState::MOVING;
                    currentDirection = Direction::DOWN;
                }
            }
        }
        break;
    }

    case ElevatorState::MOVING: {
        if (targetFloor == -1) {
            currentState = ElevatorState::IDLE;
            break;
        }
        float targetY = (float)targetFloor * FLOOR_HEIGHT;
        if (abs(elevatorY - targetY) < ELEVATOR_SPEED) {
            elevatorY = targetY;
            currentState = ElevatorState::STOPPED;
            stateTimer = GetTickCount();
        }
        else {
            if (elevatorY > targetY) elevatorY -= ELEVATOR_SPEED;
            else elevatorY += ELEVATOR_SPEED;
            if (currentDirection == Direction::DOWN && targetFloor == FLOOR_COUNT - 1) {
                int currentFloor = GetFloorFromY(elevatorY);
                for (const auto& p : passengers) {
                    if (!p.inElevator && !p.boarding && p.startFloor >= currentFloor && p.startFloor < targetFloor) {
                        targetFloor = p.startFloor;
                        currentState = ElevatorState::MOVING;
                        return;
                    }
                }
            }
        }
        break;
    }


    case ElevatorState::STOPPED: {
        if (GetTickCount() - stateTimer > DOOR_OPEN_DURATION) {
            int currentFloor = GetFloorFromY(elevatorY);
            for (auto it = passengers.begin(); it != passengers.end();) {
                if (it->inElevator && it->targetFloor == currentFloor) {
                    it = passengers.erase(it);
                }
                else {
                    ++it;
                }
            }

            float weight = 0;
            for (const auto& p : passengers) {
                if (p.inElevator || p.boarding) weight += WEIGHT_PER_PERSON;
            }

            Direction boardingDirection = currentDirection;
            if (boardingDirection == Direction::NONE) {
                bool wants_up = false, wants_down = false;
                for (const auto& p : passengers) {
                    if (!p.inElevator && !p.boarding && p.startFloor == currentFloor) {
                        if (p.targetFloor < currentFloor) wants_up = true;
                        else wants_down = true;
                    }
                }
                if (wants_up) boardingDirection = Direction::UP;
                else if (wants_down) boardingDirection = Direction::DOWN;
            }

            int elevatorX = UI_WALL_START_X + UI_WALL_WIDTH - 150;

            if (boardingDirection == Direction::UP) {
                for (auto& p : passengers) {
                    if (!p.inElevator && !p.boarding && p.startFloor == currentFloor && p.targetFloor < currentFloor) {
                        if (weight + WEIGHT_PER_PERSON <= MAX_WEIGHT) {

                            p.boarding = true;
                            p.boardingProgress = 0.0f;

                            int baseX = UI_WALL_START_X + 20;
                            int baseY = p.startFloor * FLOOR_HEIGHT + (FLOOR_HEIGHT - PASSENGER_TOTAL_HEIGHT);
                            int cx = baseX + (p.posIndex * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING));
                            p.animX = (float)cx;
                            p.animY = (float)baseY;

                            weight += WEIGHT_PER_PERSON;
                        }
                    }
                }
            }

            if (boardingDirection == Direction::DOWN) {
                for (auto& p : passengers) {

                    if (!p.inElevator && !p.boarding && p.startFloor == currentFloor && p.targetFloor > currentFloor) {
                        if (weight + WEIGHT_PER_PERSON <= MAX_WEIGHT) {
                            p.boarding = true;
                            p.boardingProgress = 0.0f;

                            int baseX = UI_WALL_START_X + 20;
                            int baseY = p.startFloor * FLOOR_HEIGHT + (FLOOR_HEIGHT - PASSENGER_TOTAL_HEIGHT);
                            int cx = baseX + (p.posIndex * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING));
                            p.animX = (float)cx;
                            p.animY = (float)baseY;

                            weight += WEIGHT_PER_PERSON;
                        }
                    }
                }
            }

            if (currentDirection == Direction::NONE && boardingDirection != Direction::NONE) {
                currentDirection = boardingDirection;
            }

            ReorganizePositions();
            DecideNextMove();
        }
        break;
    }

    }
}

void DrawStickFigure(Graphics& g, int x, int y, Color headColor, Color bodyColor) {
    Pen bodyPen(bodyColor, 2);
    SolidBrush headBrush(headColor);
    g.FillEllipse(&headBrush, x, y, PASSENGER_HEAD_RADIUS * 2, PASSENGER_HEAD_RADIUS * 2);
    g.DrawEllipse(&bodyPen, x, y, PASSENGER_HEAD_RADIUS * 2, PASSENGER_HEAD_RADIUS * 2);
    int headCenterX = x + PASSENGER_HEAD_RADIUS;
    int bodyTopY = y + PASSENGER_HEAD_RADIUS * 2;
    int bodyBottomY = bodyTopY + PASSENGER_BODY_HEIGHT;
    g.DrawLine(&bodyPen, headCenterX, bodyTopY, headCenterX, bodyBottomY);
    g.DrawLine(&bodyPen, headCenterX - PASSENGER_ARM_SPAN / 2, bodyTopY + PASSENGER_BODY_HEIGHT / 2, headCenterX + PASSENGER_ARM_SPAN / 2, bodyTopY + PASSENGER_BODY_HEIGHT / 2);
    g.DrawLine(&bodyPen, headCenterX, bodyBottomY, headCenterX - PASSENGER_LEG_LENGTH / 2, bodyBottomY + PASSENGER_LEG_LENGTH);
    g.DrawLine(&bodyPen, headCenterX, bodyBottomY, headCenterX + PASSENGER_LEG_LENGTH / 2, bodyBottomY + PASSENGER_LEG_LENGTH);
}

void DrawScene(Graphics& g) {
    g.Clear(Color::White);
    SolidBrush wallBrush(Color(220, 220, 220));
    Pen floorPen(Color::DarkGray, 2);
    Font floorFont(L"Arial", 14);
    SolidBrush blackBrush(Color::Black);
    for (int i = 0; i < FLOOR_COUNT; i++) {
        int y = i * FLOOR_HEIGHT;
        g.FillRectangle(&wallBrush, UI_WALL_START_X, y, UI_WALL_WIDTH, FLOOR_HEIGHT);
        g.DrawLine(&floorPen, UI_WALL_START_X, y, UI_WALL_START_X + UI_WALL_WIDTH, y);

        WCHAR buf[32];
        wsprintf(buf, L"Piętro %d", FLOOR_COUNT - 1 - i);
        g.DrawString(buf, -1, &floorFont, PointF(10, (float)(y + 30)), &blackBrush);
    }
    g.DrawLine(&floorPen, UI_WALL_START_X, FLOOR_COUNT * FLOOR_HEIGHT, UI_WALL_START_X + UI_WALL_WIDTH, FLOOR_COUNT * FLOOR_HEIGHT);
    int elevatorX = UI_WALL_START_X + UI_WALL_WIDTH - 150;
    int elevatorScreenY = (int)elevatorY + FLOOR_HEIGHT - ELEVATOR_HEIGHT;
    LinearGradientBrush elevatorBrush(Rect(elevatorX, elevatorScreenY, ELEVATOR_WIDTH, ELEVATOR_HEIGHT), Color(200, 200, 200), Color(150, 150, 150), LinearGradientModeVertical);
    Pen elevatorPen(Color::Black, 3);
    g.FillRectangle(&elevatorBrush, elevatorX, elevatorScreenY, ELEVATOR_WIDTH, ELEVATOR_HEIGHT);
    g.DrawRectangle(&elevatorPen, elevatorX, elevatorScreenY, ELEVATOR_WIDTH, ELEVATOR_HEIGHT);
    Pen doorPen(Color::DarkSlateGray, 2);
    g.DrawLine(&doorPen, elevatorX + ELEVATOR_WIDTH / 2, elevatorScreenY, elevatorX + ELEVATOR_WIDTH / 2, elevatorScreenY + ELEVATOR_HEIGHT);
    float currentWeight = 0.0f;
    int passengersInElevator = 0;
    for (const auto& p : passengers) if (p.inElevator) {
        currentWeight += WEIGHT_PER_PERSON;
        passengersInElevator++;
    }

    Font targetFont(L"Arial", 10);
    Font targetFontSmall(L"Arial", 8);

    float totalPassengersWidth = passengersInElevator * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING);
    float startOffset = (ELEVATOR_WIDTH - totalPassengersWidth) / 2.0f;
    if (startOffset < 0) startOffset = 0;
    for (const auto& p : passengers) {
        WCHAR targetBuf[16];
        swprintf(targetBuf, 16, L"-> %d", FLOOR_COUNT - 1 - p.targetFloor);

        if (p.inElevator) {
            int px = elevatorX + (p.posIndex * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING));
            int py = elevatorScreenY + ELEVATOR_HEIGHT - PASSENGER_TOTAL_HEIGHT;
            if (px + PASSENGER_TOTAL_WIDTH > elevatorX + ELEVATOR_WIDTH) continue;
            DrawStickFigure(g, px, py, Color::Blue, Color::DarkBlue);
            swprintf(targetBuf, 16, L"%d", FLOOR_COUNT - 1 - p.targetFloor);
            SolidBrush targetBrush(Color::White);
            g.DrawString(targetBuf, -1, &targetFontSmall, PointF((float)(px + PASSENGER_HEAD_RADIUS - 5), (float)(py - 10)), &targetBrush);
        }
        else if (p.boarding) {
            DrawStickFigure(g, (int)p.animX, (int)p.animY, Color::Green, Color::DarkGreen);
            SolidBrush targetBrush(Color::Black);
            g.DrawString(targetBuf, -1, &targetFont, PointF(p.animX + PASSENGER_TOTAL_WIDTH, p.animY + PASSENGER_HEAD_RADIUS), &targetBrush);
        }
        else {
            int baseX = UI_WALL_START_X + 20;
            int baseY = p.startFloor * FLOOR_HEIGHT + (FLOOR_HEIGHT - PASSENGER_TOTAL_HEIGHT);
            int cx = baseX + (p.posIndex * (PASSENGER_TOTAL_WIDTH + PASSENGER_SPACING));
            DrawStickFigure(g, cx, baseY, Color::Green, Color::DarkGreen);
            SolidBrush targetBrush(Color::Black);
            g.DrawString(targetBuf, -1, &targetFont, PointF((float)(cx + PASSENGER_TOTAL_WIDTH), (float)(baseY + PASSENGER_HEAD_RADIUS)), &targetBrush);
        }
    }
    WCHAR buf[128];
    swprintf(buf, 128, L"Aktualna masa: %.0f kg (Max: %.0f kg)", currentWeight, MAX_WEIGHT);
    Font infoFont(L"Arial", 12);
    g.DrawString(buf, -1, &infoFont, PointF(10, (float)FLOOR_COUNT * FLOOR_HEIGHT + 10), &blackBrush);

    WCHAR info[] = L"Wybierz piętro docelowe, aby przywołać pasażera.";
    g.DrawString(info, -1, &infoFont, PointF(10, (float)FLOOR_COUNT * FLOOR_HEIGHT + 35), &blackBrush);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        if (memDC) {
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        }
        HDC hdc = GetDC(hwnd);
        memDC = CreateCompatibleDC(hdc);
        memBitmap = CreateCompatibleBitmap(hdc, width, height);
        SelectObject(memDC, memBitmap);
        ReleaseDC(hwnd, hdc);
        memWidth = width;
        memHeight = height;
        return 0;
    }
    case WM_TIMER:
        MoveElevator();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        const int BUTTON_ID_BASE = 100;
        if (id >= BUTTON_ID_BASE && id < BUTTON_ID_BASE + (FLOOR_COUNT * FLOOR_COUNT)) {
            int startFloor = (id - BUTTON_ID_BASE) / FLOOR_COUNT;
            int targetFloor = (id - BUTTON_ID_BASE) % FLOOR_COUNT;

            if (startFloor == targetFloor) return 0;

            passengers.push_back({ nextPassengerId++, startFloor, targetFloor });
            ReorganizePositions();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(memDC);
        DrawScene(g);
        BitBlt(hdc, 0, 0, memWidth, memHeight, memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (memDC) {
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        }
        GdiplusShutdown(gdiplusToken);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ElevatorSimWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    int windowWidth = UI_BUTTON_AREA_START_X + (FLOOR_COUNT - 1) * (UI_BUTTON_WIDTH + UI_BUTTON_SPACING_X) + 50;
    int windowHeight = FLOOR_COUNT * FLOOR_HEIGHT + 100;

    hwndGlobal = CreateWindowEx(0, wc.lpszClassName, L"Symulator Windy", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwndGlobal) return -1;

    ShowWindow(hwndGlobal, nCmdShow);
    UpdateWindow(hwndGlobal);

    const int BUTTON_ID_BASE = 100;
    for (int mainFloor = 0; mainFloor < FLOOR_COUNT; ++mainFloor) {
        int btnGroupY = mainFloor * FLOOR_HEIGHT + 20;
        int currentButtonInRow = 0;
        for (int targetFloor = 0; targetFloor < FLOOR_COUNT; ++targetFloor) {
            if (mainFloor == targetFloor) continue;
            WCHAR buttonText[32];
            swprintf(buttonText, 32, L"%d -> %d", FLOOR_COUNT - 1 - mainFloor, FLOOR_COUNT - 1 - targetFloor);
            int btnX = UI_BUTTON_AREA_START_X + (currentButtonInRow * (UI_BUTTON_WIDTH + UI_BUTTON_SPACING_X));
            CreateWindow(L"BUTTON", buttonText,
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                btnX, btnGroupY, UI_BUTTON_WIDTH, UI_BUTTON_HEIGHT,
                hwndGlobal, (HMENU)(BUTTON_ID_BASE + (mainFloor * FLOOR_COUNT) + targetFloor),
                hInstance, nullptr);
            currentButtonInRow++;
        }
    }

    SetTimer(hwndGlobal, 1, 33, nullptr);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}