#pragma once

// The v4.1.1 DevPkg references DirectX-Headers at this path but omits that
// header tree. Forward to the installed Windows SDK's D3D12 header so the
// standalone plugin can compile without adding a dependency.
#include <d3d12.h>
