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
#include <filesystem>
#include <iostream>
#include <string_view>
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

ComPtr<IPixGpuCaptureDocument> OpenCapture(const wchar_t* captureFilename)
{
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    if (!SetDllDirectoryW(PixInstallationDirectory))
    {
        Check(HRESULT_FROM_WIN32(GetLastError()), "SetDllDirectoryW");
    }

    ComPtr<IPixFactory> factory;
    Check(PixCreateFactory(IID_PPV_ARGS(&factory)), "PixCreateFactory");
    ComPtr<IPixGpuCaptureDocument> document;
    Check(factory->OpenGpuCaptureDocument(std::filesystem::absolute(captureFilename).c_str(), nullptr, nullptr, IID_PPV_ARGS(&document)), "OpenGpuCaptureDocument");
    return document;
}

void PrintOverlappingBuffers(const wchar_t* captureFilename, uint64_t bufferApiObjectId)
{
    const auto document = OpenCapture(captureFilename);
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

std::vector<BYTE> ReadBuffer(IPixGpuCaptureAnalysis* analysis, IPixD3D12Resource* buffer, const PIX_EVENT_INFO& event, BOOL beforeEvent)
{
    const PIX_GET_BUFFER_BYTES_PARAMETERS parameters = { beforeEvent, FALSE };
    ComPtr<IPixBlob> blob;
    if (FAILED(analysis->GetBufferBytesFromResource(buffer, &event, &parameters, IID_PPV_ARGS(&blob))))
    {
        // Events without a command list, or where the buffer doesn't exist yet, can't be read.
        return {};
    }

    std::vector<BYTE> bytes(blob->GetSizeBytes());
    Check(blob->GetData(bytes.size(), bytes.data()), "GetData");
    return bytes;
}

void PrintEventsThatModifyBuffer(const wchar_t* captureFilename, uint64_t bufferApiObjectId)
{
    const auto document = OpenCapture(captureFilename);
    ComPtr<IPixD3D12Resources> resources;
    Check(document->GetD3D12Resources(IID_PPV_ARGS(&resources)), "GetD3D12Resources");

    ComPtr<IPixD3D12Resource> buffer;
    for (UINT32 resourceIndex = 0; resourceIndex < resources->GetCount(); ++resourceIndex)
    {
        ComPtr<IPixD3D12Resource> resource;
        Check(resources->GetD3D12Resource(resourceIndex, IID_PPV_ARGS(&resource)), "GetD3D12Resource");
        if (resource->GetApiObjectId() == static_cast<PIX_API_OBJECT_ID>(bufferApiObjectId) &&
            resource->GetResourceDesc().Desc2->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            buffer = resource;
        }
    }

    if (!buffer)
    {
        std::cerr << "No buffer with API object ID " << bufferApiObjectId << ".\n";
        std::exit(1);
    }

    ComPtr<IPixGpuCaptureAnalysis> analysis;
    Check(document->GetAnalysis(IID_PPV_ARGS(&analysis)), "GetAnalysis");
    PIX_CONNECTION_DESC_LOCAL localConnection = {};
    PIX_CONNECTION_DESC connection = {};
    connection.Type = PIX_CONNECTION_TYPE_LOCAL;
    connection.pLocal = &localConnection;
    Check(analysis->Connect(&connection, nullptr), "Connect");
    Check(analysis->StartAnalysis(nullptr, nullptr, nullptr), "StartAnalysis");

    ComPtr<IPixCollection> queues;
    Check(document->GetQueues(IID_PPV_ARGS(&queues)), "GetQueues");
    for (UINT64 queueIndex = 0; queueIndex < queues->GetCount(); ++queueIndex)
    {
        ComPtr<IPixGpuCaptureQueueInfo> queue;
        Check(queues->Get(queueIndex, IID_PPV_ARGS(&queue)), "Get(IPixGpuCaptureQueueInfo)");

        std::vector<BYTE> previousContents;
        for (UINT32 eventIndex = 0; eventIndex < queue->GetEventCount(); ++eventIndex)
        {
            PIX_EVENT_INFO event = {};
            Check(queue->GetEvent(eventIndex, &event), "GetEvent");
            if (previousContents.empty())
            {
                previousContents = ReadBuffer(analysis.Get(), buffer.Get(), event, TRUE);
            }

            auto contents = ReadBuffer(analysis.Get(), buffer.Get(), event, FALSE);
            if (contents.empty())
            {
                continue;
            }

            if (!previousContents.empty() && contents != previousContents)
            {
                std::cout << event.GpuId << ": " << event.Name << '\n';
            }

            previousContents = std::move(contents);
        }
    }
}

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount == 4 && std::wstring_view(arguments[3]) == L"--writes")
    {
        PrintEventsThatModifyBuffer(arguments[1], _wtoi64(arguments[2]));
        return 0;
    }

    if (argumentCount != 3)
    {
        std::cerr << "Usage: BufferOverlaps.exe <capture.wpix> <buffer-api-object-id> [--writes]\n";
        return 2;
    }

    PrintOverlappingBuffers(arguments[1], _wtoi64(arguments[2]));
    return 0;
}
