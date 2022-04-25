#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"

#include <stdexcept>

#ifdef _DEBUG
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

struct Vertex
{
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT4 col;
};

unsigned int width = 1280, height = 800;
float aspectRatio;
enum
{
	frameCount = 2,
};

HWND window;

CD3DX12_VIEWPORT viewport;
CD3DX12_RECT scissorRect;

ID3D12Device *device;
IDXGISwapChain3 *swapChain;
ID3D12CommandQueue *cmdQueue;
ID3D12RootSignature *rootSig;
ID3D12DescriptorHeap *rtvHeap;
ID3D12GraphicsCommandList *cmdList;
ID3D12PipelineState * pipelineState;
ID3D12CommandAllocator *cmdAllocator;
ID3D12Resource *renderTargets[frameCount];
UINT rtvDescriptorSize;

ID3D12Resource *vertBuff;
D3D12_VERTEX_BUFFER_VIEW vertBuffView;

int frameIdx;
HANDLE fenceEvent;
UINT64 fenceValue;
ID3D12Fence *fence;

void Try(HRESULT hr);
void GetHardwareAdapter(IDXGIFactory1 *factory, IDXGIAdapter1 **adapterPtr, DXGI_GPU_PREFERENCE gpuPref = DXGI_GPU_PREFERENCE_UNSPECIFIED);
void LoadPipeline();
void LoadAssets();
void PopulateCmdList();
void WaitForPrevFrame();
void CompileShader(HRESULT hr, ID3DBlob **errPtr);
LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int argc, char *argv[])
{
	WNDCLASSEXA cls = WNDCLASSEXA {
		sizeof(WNDCLASSEXA),
	};

	cls.style = CS_CLASSDC;
	cls.lpfnWndProc = WndProc;
	cls.lpszClassName = "Game";
	cls.hInstance = GetModuleHandleA(NULL);
	RegisterClassExA(&cls);

	window = CreateWindowExA(WS_EX_APPWINDOW, cls.lpszClassName, "PlebCraft - Treidex", WS_OVERLAPPEDWINDOW, 100, 100, width, height, NULL, NULL, cls.hInstance, NULL);

	// todo: yes
	frameIdx = 0;
	viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float) width, (float) height);
	scissorRect = CD3DX12_RECT(0, 0, (LONG) width, (LONG) height);
	rtvDescriptorSize = 0;

	aspectRatio = (float) width / (float) height;

	LoadPipeline();
	LoadAssets();

	ShowWindow(window, SW_SHOWDEFAULT);
	UpdateWindow(window);

	while (true)
	{
		MSG msg;
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);

			if (msg.message == WM_QUIT)
				goto end;
		}
	}

end:
	WaitForPrevFrame();
	CloseHandle(fenceEvent);

	DestroyWindow(window);
	UnregisterClassA(cls.lpszClassName, cls.hInstance);
}

void LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	{
		ID3D12Debug *debugController = NULL;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	IDXGIFactory4 *factory;
	Try(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	IDXGIAdapter1 *hardwareAdapter;
	GetHardwareAdapter(factory, &hardwareAdapter);

	Try(D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

	// make cmd queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	Try(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));

	// make swap chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.Width = width,
		.Height = height,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = { .Count = 1, },
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = frameCount,

		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
	};


	Try(factory->CreateSwapChainForHwnd(cmdQueue, window, &swapChainDesc, nullptr, nullptr, (IDXGISwapChain1 **) &swapChain));
	frameIdx = swapChain->GetCurrentBackBufferIndex();

	// disable fullscreen transitions
	Try(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));

	// create descriptor heaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = frameCount,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};

		Try(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
		rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);
	}

	// create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

		for (int i = 0; i < frameCount; i++)
		{
			Try(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));
			device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
			rtvHandle.Offset(1, rtvDescriptorSize);
		}
	}

	Try(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator)));
}

void LoadAssets()
{
	// create an empty root sig
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ID3DBlob *sig, *err;
		Try(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
		Try(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
	}

	// create the pipeline state, including compiling & loading shaders
	static constexpr const wchar_t *shaderPath = L"res/shaders.hlsl";
	{
		ID3DBlob *vertShader, *pixelShader;

		UINT compileFlags =
	#ifdef _DEBUG
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	#else
		0;
	#endif

		ID3DBlob *err = nullptr;
		CompileShader(D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, &vertShader, &err), &err);
		CompileShader(D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &err), &err);

		// define vertex input layout
		D3D12_INPUT_ELEMENT_DESC inputEleDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0, },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0, },
		};

		// describe & create graphics pipeline state object (PSO)
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
			.pRootSignature = rootSig,
			.VS = CD3DX12_SHADER_BYTECODE(vertShader),
			.PS = CD3DX12_SHADER_BYTECODE(pixelShader),
			.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
			.SampleMask = UINT_MAX,
			.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
			.DepthStencilState = {
				.DepthEnable = FALSE,
				.StencilEnable = FALSE,
			},
			.InputLayout = { inputEleDescs, _countof(inputEleDescs), },
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM, },
			.SampleDesc = { .Count = 1 },
		};

		Try(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
	}

	// create cmd list
	Try(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocator, pipelineState, IID_PPV_ARGS(&cmdList)));
	Try(cmdList->Close());

	// create vert buff
	{
		Vertex verts[] = {
			{
				.pos = { 0.0f, 0.25f * aspectRatio, 0.0f, },
				.col = { 1.0f, 0.0f, 0.0f, 1.0f, },
			},
			{
				.pos = { 0.25f, -0.25f * aspectRatio, 0.0f, },
				.col = { 0.0f, 1.0f, 0.0f, 1.0f, },
			},
			{
				.pos = { -0.25f, -0.25f * aspectRatio, 0.0f, },
				.col = { 0.0f, 0.0f, 1.0f, 1.0f, },
			},
		};

		const UINT vertBuffSz = sizeof(verts);

		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(vertBuffSz);
		Try(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&vertBuff)
		));

		// copy vertex data into vert buff
		UINT8 *vertData;
		CD3DX12_RANGE readRange(0, 0); // do not intend to read on CPU
		Try(vertBuff->Map(0, &readRange, (void **) &vertData));
		memcpy(vertData, verts, sizeof(verts));
		vertBuff->Unmap(0, nullptr);

		// init vert buff view
		vertBuffView.BufferLocation = vertBuff->GetGPUVirtualAddress();
		vertBuffView.StrideInBytes = sizeof(Vertex);
		vertBuffView.SizeInBytes = vertBuffSz;
	}

	// create syncing objs & wait until assets have uploaded to GPU
	{
		Try(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		fenceValue = 1;

		fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
		if (fenceEvent == nullptr)
			Try(HRESULT_FROM_WIN32(GetLastError()));

		WaitForPrevFrame();
	}
}

// todo study
void PopulateCmdList()
{
	Try(cmdAllocator->Reset());
	Try(cmdList->Reset(cmdAllocator, pipelineState));

	cmdList->SetGraphicsRootSignature(rootSig);
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissorRect);

	CD3DX12_RESOURCE_BARRIER resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->ResourceBarrier(1, &resBarrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIdx, rtvDescriptorSize);
	cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	const float clearColor[] = { 0.25f, 0.25f, 0.25f, 1.0f };
	cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &vertBuffView);
	cmdList->DrawInstanced(3, 1, 0, 0);

	resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	cmdList->ResourceBarrier(1, &resBarrier);

	Try(cmdList->Close());
}

void CompileShader(HRESULT hr, ID3DBlob **errPtr)
{
	if (!FAILED(hr))
		return;

	if (errPtr && *errPtr)
	{
		const char *error = (const char *) (*errPtr)->GetBufferPointer();
		puts(error);
		throw std::exception(error);
	}
	else
		Try(hr);
}

void WaitForPrevFrame()
{
	Try(cmdQueue->Signal(fence, fenceValue));

	if (fence->GetCompletedValue() < fenceValue)
	{
		Try(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	fenceValue++;
	frameIdx = swapChain->GetCurrentBackBufferIndex();
}

void GetHardwareAdapter(IDXGIFactory1 *factory, IDXGIAdapter1 **adapterPtr, DXGI_GPU_PREFERENCE gpuPref)
{
	*adapterPtr = nullptr;

	IDXGIAdapter1 *adapter = nullptr;

	IDXGIFactory6 *factory6;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(i, gpuPref, IID_PPV_ARGS(&adapter))); i++)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
				break;
		}
	}

	if (adapter == nullptr)
	{
		for (UINT i = 0; SUCCEEDED(factory6->EnumAdapters1(i, &adapter)); i++)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
				break;
		}
	}

	*adapterPtr = adapter;
}

std::string HrToString(HRESULT hr)
{
	char s_str[64] = {};
	sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
	return std::string(s_str);
}


void Try(HRESULT hr)
{
	if (!FAILED(hr))
		return;

	throw std::exception(HrToString(hr).c_str());
}


LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_PAINT:
	{
		PopulateCmdList();

		ID3D12CommandList *cmdLists[] = { cmdList };
		cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

		Try(swapChain->Present(1, 0));
		WaitForPrevFrame();
		return 0;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcA(wnd, msg, wParam, lParam);
}