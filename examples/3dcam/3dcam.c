// Umka embedded scripting example (original raylib example by Ramon Santamaria) 

#include <stdio.h>
#include "raylib.h"
#include "umka_api.h"


// Umka extension functions
// The order of the parameters is counted from the end, not the beginning. if your function is `add(x, y: int)`, x is on params[1] and y is on params[0].
void rlDrawPlane(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Color *color = (Color *)&params[0];
    Vector2 *size = (Vector2 *)&params[1];
    Vector3 *centerPos = (Vector3 *)&params[2];

    DrawPlane(*centerPos, *size, *color);
}


void rlDrawCube(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Color *color = (Color *)&params[0];
    float length = params[1].realVal;
    float height = params[2].realVal;
    float width = params[3].realVal;
    Vector3 *position = (Vector3 *)&params[4];
      
    DrawCube(*position, width, height, length, *color);
}


int main(void)
{
    // Umka initialization
    int umkaInitBodies = 0, umkaDrawBodies = 0;
    void *umka = umkaAlloc();
    bool umkaOk = umkaInit(umka, "3dcam.um", NULL, 1024 * 1024, NULL, 0, NULL, false, false, NULL);
    
    if (umkaOk)
    {
        umkaAddFunc(umka, "drawPlane", &rlDrawPlane);
        umkaAddFunc(umka, "drawCube", &rlDrawCube);
        umkaOk = umkaCompile(umka);
    }
        
    if (umkaOk)
    {
        printf("Umka initialized\n");
        umkaInitBodies = umkaGetFunc(umka, NULL, "initBodies"); 
        umkaDrawBodies = umkaGetFunc(umka, NULL, "drawBodies");
    }
    else
    {
        UmkaError error;
        umkaGetError(umka, &error);
        printf("Umka error %s (%d, %d): %s\n", error.fileName, error.line, error.pos, error.msg);
    }
    
    // Raylib initialization
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "raylib [core] example - 3d camera first person");

    // Define the camera to look into our 3D world (position, target, up vector)
    Camera camera = { 0 };
    camera.position = (Vector3){ 4.0f, 2.0f, 4.0f };
    camera.target = (Vector3){ 0.0f, 1.8f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;

    if (umkaOk)
        umkaOk = umkaCall(umka, umkaInitBodies, 0, NULL, NULL);
        
    if (!umkaOk)
    {
        UmkaError error;
        umkaGetError(umka, &error);
        printf("Umka runtime error %s (%d): %s\n", error.fileName, error.line, error.msg);
    }        

    SetCameraMode(camera, CAMERA_FIRST_PERSON); // Set a first person camera mode
    SetTargetFPS(60);                           // Set our game to run at 60 frames per second

    // Main game loop
    if (umkaOk)  
        while (!WindowShouldClose())            // Detect window close button or ESC key
        {
            UpdateCamera(&camera);
            BeginDrawing();

                ClearBackground((Color){ 190, 190, 255, 255 });

                BeginMode3D(camera);

                bool umkaOk = umkaCall(umka, umkaDrawBodies, 0, NULL, NULL);
                if (!umkaOk)
                {
                    UmkaError error;
                    umkaGetError(umka, &error);
                    printf("Umka runtime error %s (%d): %s\n", error.fileName, error.line, error.msg);
                    break;
                }            

                EndMode3D();

                DrawRectangle( 10, 10, 220, 70, Fade(SKYBLUE, 0.5f));
                DrawRectangleLines( 10, 10, 220, 70, BLUE);

                DrawText("First person camera default controls:", 20, 20, 10, BLACK);
                DrawText("- Move with keys: W, A, S, D", 40, 40, 10, DARKGRAY);
                DrawText("- Mouse move to look around", 40, 60, 10, DARKGRAY);

            EndDrawing();
        }

    // Raylib de-initialization
    CloseWindow();        // Close window and OpenGL context

    // Umka de-initialization
    umkaFree(umka);
        
    return 0;
}
