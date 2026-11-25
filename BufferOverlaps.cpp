#include <Windows.h>

// Change these if PIX is installed somewhere else.
static const wchar_t* PixInstallationDirectory = LR"(C:\Program Files\Microsoft PIX\2611.19)";
#include "C:\Program Files\Microsoft PIX\2611.19\include\PixApi.h"
#pragma comment(lib, R"(C:\Program Files\Microsoft PIX\2611.19\lib\PixApi.lib)")

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "delayimp.lib")

#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;

struct Buffer
{
    PIX_API_OBJECT_ID ApiObjectId;
    PIX_API_OBJECT_ID HeapApiObjectId;
    uint64_t Begin;
    uint64_t End;
};

void Check(HRESULT status, const char* operation)
{
    if (FAILED(status))
    {
        std::cerr << operation << " failed: 0x" << std::hex << status << '\n';
        std::exit(1);
    }
}

void PrintOverlappingBuffers(const wchar_t* captureFilename, uint64_t bufferApiObjectId)
{
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    if (!SetDllDirectoryW(PixInstallationDirectory))
    {
        Check(HRESULT_FROM_WIN32(GetLastError()), "SetDllDirectoryW");
    }

    ComPtr<IPixFactory> factory;
    Check(PixCreateFactory(IID_PPV_ARGS(&factory)), "PixCreateFactory");
    ComPtr<IPixGpuCaptureDocument> document;
    Check(factory->OpenGpuCaptureDocument(captureFilename, nullptr, nullptr, IID_PPV_ARGS(&document)), "OpenGpuCaptureDocument");
    ComPtr<IPixD3D12Resources> resources;
    Check(document->GetD3D12Resources(IID_PPV_ARGS(&resources)), "GetD3D12Resources");

    std::vector<Buffer> buffers;
    for (UINT32 resourceIndex = 0; resourceIndex < resources->GetCount(); ++resourceIndex)
    {
        ComPtr<IPixD3D12Resource> resource;
        Check(resources->GetD3D12Resource(resourceIndex, IID_PPV_ARGS(&resource)), "GetD3D12Resource");
        const auto* description = resource->GetResourceDesc().Desc2;
        if (description->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
            resource->GetType() != PIX_D3D12_RESOURCE_PLACED)
        {
            continue;
        }

        ComPtr<IPixPlacedD3D12Resource> placedResource;
        Check(resource.As(&placedResource), "QueryInterface(IPixPlacedD3D12Resource)");
        const uint64_t begin = placedResource->GetHeapOffset();
        buffers.push_back({
            resource->GetApiObjectId(),
            placedResource->GetHeapId(),
            begin,
            begin + description->Width });
    }

    std::ranges::sort(buffers, {}, &Buffer::ApiObjectId);
    const auto target = std::ranges::find(buffers, static_cast<PIX_API_OBJECT_ID>(bufferApiObjectId), &Buffer::ApiObjectId);
    if (target == buffers.end())
    {
        std::cerr << "No placed buffer with API object ID " << bufferApiObjectId << ".\n";
        std::exit(1);
    }

    for (const auto& buffer : buffers)
    {
        if (buffer.ApiObjectId != target->ApiObjectId && buffer.HeapApiObjectId == target->HeapApiObjectId &&
            buffer.Begin < target->End && target->Begin < buffer.End)
        {
            std::cout << static_cast<uint64_t>(buffer.ApiObjectId) << '\n';
        }
    }
}

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount != 3)
    {
        std::cerr << "Usage: BufferOverlaps.exe <capture.wpix> <buffer-api-object-id>\n";
        return 2;
    }

    PrintOverlappingBuffers(arguments[1], _wtoi64(arguments[2]));
    return 0;
}
