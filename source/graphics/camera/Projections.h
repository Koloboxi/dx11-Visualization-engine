#pragma once
#include <DirectXMath.h>
using namespace DirectX;

namespace Projections {
    const XMMATRIX XY = XMMatrixSet(
        -1, 0, 0, 0.0f,
         0, 0, 1, 0.0f,
         0,-1, 0, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    const XMMATRIX XY_BOT = XMMatrixSet(
         1, 0, 0, 0.0f,
         0, 0,-1, 0.0f,
         0, 1, 0, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    const XMMATRIX XZ = XMMatrixSet(
        -1, 0, 0, 0.0f,
         0, 1, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    const XMMATRIX XZ_BACK = XMMatrixSet(
         1, 0, 0, 0.0f,
         0,-1, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
    const XMMATRIX YZ = XMMatrixSet(
         0, 1, 0, 0.0f,
         1, 0, 0, 0.0f,
         0, 0, 1, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    );
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
