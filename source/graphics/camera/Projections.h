#pragma once
#include <DirectXMath.h>
using namespace DirectX;

namespace Projections {
    // Look from +Z direction (top view, shows XY plane)
    const XMMATRIX XY = XMMatrixSet(
        -1, 0, 0, 0.0f,
         0, 0, 1, 0.0f,
         0,-1, 0, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    // Look from -Z direction (bottom view)
    const XMMATRIX XY_BOT = XMMatrixSet(
         1, 0, 0, 0.0f,
         0, 0,-1, 0.0f,
         0, 1, 0, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    // Look from +Y direction (front view, shows XZ plane)
    const XMMATRIX XZ = XMMatrixSet(
        -1, 0, 0, 0.0f,
         0, 1, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    // Look from -Y direction (back view)
    const XMMATRIX XZ_BACK = XMMatrixSet(
         1, 0, 0, 0.0f,
         0,-1, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    // Look from +X direction (right side view, shows YZ plane)
    const XMMATRIX YZ = XMMatrixSet(
         0, 1, 0, 0.0f,
         1, 0, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    // Look from -X direction (left side view)
    const XMMATRIX YZ_LEFT = XMMatrixSet(
         0,-1, 0, 0.0f,
        -1, 0, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );

    const XMMATRIX ISO = XMMatrixSet(
        -(1.0f/sqrtf(2.0f)), (1.0f/sqrtf(2.0f)), 0.0f, 0.0f,
         (1.0f/sqrtf(3.0f)), (1.0f/sqrtf(3.0f)), (1.0f/sqrtf(3.0f)), 0.0f,
        -(1.0f/sqrtf(6.0f)),-(1.0f/sqrtf(6.0f)), (sqrtf(2.0f)/sqrtf(3.0f)), 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    const XMMATRIX DIM = XMMatrixSet(
        -0.93265f, 0.35246f, 0.0f, 0.0f,
         0.2449f,  0.72801f, 0.2449f, 0.0f,
        -0.11727f,-0.31091f, 0.94f, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    ) * XMMatrixRotationRollPitchYaw(0, 0.005f, 0);
}
