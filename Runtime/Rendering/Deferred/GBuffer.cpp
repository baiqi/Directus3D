/*
Copyright(c) 2016-2018 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===================================
#include "GBuffer.h"
#include "../../RHI/RHI_Implementation.h"
#include "../../RHI/RHI_Device.h"
#include "../../RHI/D3D11/D3D11_RenderTexture.h"
//==============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	GBuffer::GBuffer(RHI* rhi, int width, int height)
	{
		m_rhi = rhi;

		m_renderTargets[GBuffer_Target_Albedo]		= make_shared<D3D11_RenderTexture>(m_rhi, width, height, false,	Texture_Format_R8G8B8A8_UNORM);
		m_renderTargets[GBuffer_Target_Normal]		= make_shared<D3D11_RenderTexture>(m_rhi, width, height, false,	Texture_Format_R8G8B8A8_UNORM);
		m_renderTargets[GBuffer_Target_Specular]	= make_shared<D3D11_RenderTexture>(m_rhi, width, height, false,	Texture_Format_R8G8B8A8_UNORM);
		m_renderTargets[GBuffer_Target_Depth]		= make_shared<D3D11_RenderTexture>(m_rhi, width, height, true,	Texture_Format_R32G32_FLOAT);
	}

	GBuffer::~GBuffer()
	{
		m_renderTargets.clear();
	}

	bool GBuffer::SetAsRenderTarget()
	{
		if (!m_rhi->GetDeviceContext())
			return false;

		// Bind the render target view array and depth stencil buffer to the output render pipeline.
		void* views[4]
		{
			m_renderTargets[GBuffer_Target_Albedo]->GetRenderTargetView(),
			m_renderTargets[GBuffer_Target_Normal]->GetRenderTargetView(),
			m_renderTargets[GBuffer_Target_Specular]->GetRenderTargetView(),
			m_renderTargets[GBuffer_Target_Depth]->GetRenderTargetView()
		};
		void** renderTargetViews = views;

		// Render Targets
		m_rhi->Bind_RenderTargets(unsigned int(m_renderTargets.size()), &renderTargetViews[0], m_renderTargets[GBuffer_Target_Depth]->GetDepthStencilView());
		// Viewport
		m_rhi->SetViewport(m_renderTargets[GBuffer_Target_Albedo]->GetViewport());

		return true;
	}

	bool GBuffer::Clear()
	{
		if (!m_rhi->GetDeviceContext())
			return false;

		// Clear the render target buffers.
		for (auto& renderTarget : m_renderTargets)
		{
			if (!renderTarget.second->GetDepthEnabled())
			{
				// Color buffer
				m_rhi->GetDeviceContext()->ClearRenderTargetView(renderTarget.second->GetRenderTargetView(), Vector4(0, 0, 0, 1).Data());
			}
			else
			{
				// Clear the depth buffer.
				m_rhi->GetDeviceContext()->ClearDepthStencilView(renderTarget.second->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, renderTarget.second->GetViewport().GetMaxDepth(), 0);
			}
		}
		return true;
	}

	void* GBuffer::GetShaderResource(GBuffer_Texture_Type type)
	{
		return (void*)m_renderTargets[type]->GetShaderResourceView();
	}
}