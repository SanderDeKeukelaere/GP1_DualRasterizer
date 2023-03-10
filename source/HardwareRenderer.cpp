#include "pch.h"
#include "HardwareRenderer.h"
#include "Mesh.h"

namespace dae {

	HardwareRenderer::HardwareRenderer(SDL_Window* pWindow) :
		m_pWindow(pWindow)
	{
		//Initialize
		SDL_GetWindowSize(pWindow, &m_Width, &m_Height);

		//Initialize DirectX pipeline
		const HRESULT result = InitializeDirectX();
		if (result == S_OK)
		{
			m_IsInitialized = true;
		}
		else
		{
			std::cout << "DirectX initialization failed!\n";
		}

		// Load the initial sample state
		const std::vector<Mesh*> tempMeshes{};
		LoadSampleState(D3D11_FILTER_MIN_MAG_MIP_POINT, tempMeshes);
	}

	HardwareRenderer::~HardwareRenderer()
	{
		if (m_pSampleState) m_pSampleState->Release();

		if (m_pRenderTargetView) m_pRenderTargetView->Release();
		if (m_pRenderTargetBuffer) m_pRenderTargetBuffer->Release();

		if (m_pDepthStencilView) m_pDepthStencilView->Release();
		if (m_pDepthStencilBuffer) m_pDepthStencilBuffer->Release();

		if (m_pSwapChain) m_pSwapChain->Release();

		if (m_pDeviceContext)
		{
			m_pDeviceContext->ClearState();
			m_pDeviceContext->Flush();
			m_pDeviceContext->Release();
		}
		if (m_pDevice) m_pDevice->Release();
	}

	ID3D11Device* HardwareRenderer::GetDevice() const
	{
		return m_pDevice;
	}

	ID3D11SamplerState* HardwareRenderer::GetSampleState() const
	{
		return m_pSampleState;
	}

	void HardwareRenderer::Render(const std::vector<Mesh*>& pMeshes, bool useUniformBackground) const
	{
		if (!m_IsInitialized)
			return;

		// Clear RTV and DSV
		const ColorRGB clearColor{ useUniformBackground ? ColorRGB{ 0.1f, 0.1f, 0.1f } : ColorRGB{ 0.39f, 0.59f, 0.93f } };
		m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, &clearColor.r);
		m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Set pipeline + Invoke drawcalls (= render)
		for (Mesh* pMesh : pMeshes)
		{
			pMesh->HardwareRender(m_pDeviceContext);
		}

		// Present backbuffer (swap)
		m_pSwapChain->Present(0, 0);
	}

	void HardwareRenderer::ToggleRenderSampleState(const std::vector<Mesh*>& pMeshes)
	{
		// Go to the next sample state
		m_SampleState = static_cast<SampleState>((static_cast<int>(m_SampleState) + 1) % (static_cast<int>(SampleState::Anisotropic) + 1));

		// Get the right D3D11 Filter
		D3D11_FILTER newFilter{};
		std::cout << "\033[32m"; // TEXT COLOR
		std::cout << "**(HARDWARE) Sampler Filter = ";
		switch (m_SampleState)
		{
		case SampleState::Point:
			std::cout << "POINT\n";
			newFilter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			break;
		case  SampleState::Linear:
			std::cout << "LINEAR\n";
			newFilter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			break;
		case  SampleState::Anisotropic:
			std::cout << "ANISOTROPIC\n";
			newFilter = D3D11_FILTER_ANISOTROPIC;
			break;
		}

		// Load the new filter in the sample state
		LoadSampleState(newFilter, pMeshes);
	}

	void HardwareRenderer::SetRasterizerState(CullMode cullMode, const std::vector<Mesh*>& pMeshes)
	{
		// Create the RasterizerState description
		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.ScissorEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;

		// Set the right cull mode
		switch (cullMode)
		{
		case CullMode::Back:
		{
			rasterizerDesc.CullMode = D3D11_CULL_BACK;
			break;
		}
		case CullMode::Front:
		{
			rasterizerDesc.CullMode = D3D11_CULL_FRONT;
			break;
		}
		case CullMode::None:
		{
			rasterizerDesc.CullMode = D3D11_CULL_NONE;
			break;
		}
		}

		// Release the current rasterizer state if one exists
		if (m_pRasterizerState) m_pRasterizerState->Release();

		// Create a new rasterizer state
		const HRESULT hr{ m_pDevice->CreateRasterizerState(&rasterizerDesc, &m_pRasterizerState) };
		if (FAILED(hr)) std::wcout << L"m_pRasterizerState failed to load\n";

		// Apply the rasterizer state
		pMeshes[0]->SetRasterizerState(m_pRasterizerState);
	}

	HRESULT HardwareRenderer::InitializeDirectX()
	{
		// Create Device and DeviceContext
		D3D_FEATURE_LEVEL featureLevel{ D3D_FEATURE_LEVEL_11_1 };
		uint32_t createDeviceFlags{ 0 };

#if defined(DEBUG) || defined(_DEBUG)
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		HRESULT result{ D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, createDeviceFlags, &featureLevel,
			1, D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pDeviceContext) };
		if (FAILED(result)) return result;

		// Create Swapchain
		// 
		// Create DXGI Factory
		IDXGIFactory1* pDxgiFactory{};
		result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pDxgiFactory));
		if (FAILED(result)) return result;

		// Create the swapchain description
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		swapChainDesc.BufferDesc.Width = m_Width;
		swapChainDesc.BufferDesc.Height = m_Height;
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.Windowed = true;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.Flags = 0;

		// Get the handle (HWND) from the SDL backbuffer
		SDL_SysWMinfo sysWMInfo{};
		SDL_VERSION(&sysWMInfo.version);
		SDL_GetWindowWMInfo(m_pWindow, &sysWMInfo);
		swapChainDesc.OutputWindow = sysWMInfo.info.win.window;

		// Create swapchain
		result = pDxgiFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
		pDxgiFactory->Release();

		if (FAILED(result)) return result;

		// Create DepthStencil (DS) and DepthStencilView (DSV)
		// Resource
		D3D11_TEXTURE2D_DESC depthStencilDesc{};
		depthStencilDesc.Width = m_Width;
		depthStencilDesc.Height = m_Height;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.ArraySize = 1;
		depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthStencilDesc.CPUAccessFlags = 0;
		depthStencilDesc.MiscFlags = 0;

		// View
		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = depthStencilDesc.Format;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the stencil buffer
		result = m_pDevice->CreateTexture2D(&depthStencilDesc, nullptr, &m_pDepthStencilBuffer);
		if (FAILED(result)) return result;

		// Create the stencil view
		result = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, &depthStencilViewDesc, &m_pDepthStencilView);
		if (FAILED(result)) return result;

		// Create RenderTarget (RT) and RenderTargetView (RTV)
		// 
		// Resource
		result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_pRenderTargetBuffer));
		if (FAILED(result)) return result;

		// View
		result = m_pDevice->CreateRenderTargetView(m_pRenderTargetBuffer, nullptr, &m_pRenderTargetView);
		if (FAILED(result)) return result;

		// Bind RTV and DSV to Output Merger Stage
		m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

		// Set viewport
		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(m_Width);
		viewport.Height = static_cast<float>(m_Height);
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.f;
		m_pDeviceContext->RSSetViewports(1, &viewport);

		return S_OK;
	}

	void HardwareRenderer::LoadSampleState(D3D11_FILTER filter, const std::vector<Mesh*>& pMeshes)
	{
		// Create the SampleState description
		D3D11_SAMPLER_DESC sampleDesc{};
		sampleDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampleDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampleDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampleDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampleDesc.MipLODBias = 0.0f;
		sampleDesc.MinLOD = -D3D11_FLOAT32_MAX;
		sampleDesc.MaxLOD = D3D11_FLOAT32_MAX;
		sampleDesc.MaxAnisotropy = 1;
		sampleDesc.Filter = filter;

		// Release the current sample state if one exists
		if (m_pSampleState) m_pSampleState->Release();

		// Create a new sample state
		const HRESULT hr{ m_pDevice->CreateSamplerState(&sampleDesc, &m_pSampleState) };
		if (FAILED(hr)) std::wcout << L"m_pSampleState failed to load\n";

		// Update the sample state in all the meshes
		for (Mesh* pMesh : pMeshes)
		{
			pMesh->SetSamplerState(m_pSampleState);
		}
	}
}
