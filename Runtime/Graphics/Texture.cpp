/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= LINKING ==========================
// Required by DDSTextureLoader when using Windows 10 SDK
//#pragma comment(lib, "WindowsApp.lib")
//====================================


//= INCLUDES ======================================
#include "Texture.h"
#include "../Core/GUIDGenerator.h"
#include "../IO/Serializer.h"
#include "../Logging/Log.h"
#include "../Core/Helper.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/DDSTextureImporter.h"
//================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

Texture::Texture(Context* context)
{
	// IResource
	m_resourceID = GENERATE_GUID;
	m_resourceType = Texture_Resource;

	// Texture
	m_context = context;
	m_width = 0;
	m_height = 0;
	m_textureType = Albedo;
	m_grayscale = false;
	m_transparency = false;
	m_alphaIsTransparency = false;
	m_generateMipchain = true;
	m_texture = make_unique<D3D11Texture>(m_context->GetSubsystem<GraphicsDevice>());
}

Texture::~Texture()
{

}

//= IO ========================================================
bool Texture::SaveMetadata()
{
	if (!Serializer::StartWriting(GetFilePathMetadata()))
		return false;

	Serializer::WriteSTR(METADATA_TYPE_TEXTURE);
	Serializer::WriteSTR(m_resourceID);
	Serializer::WriteSTR(m_resourceName);
	Serializer::WriteSTR(m_resourceFilePath);
	Serializer::WriteInt(m_width);
	Serializer::WriteInt(m_height);
	Serializer::WriteInt(int(m_textureType));
	Serializer::WriteBool(m_grayscale);
	Serializer::WriteBool(m_transparency);
	Serializer::WriteBool(m_generateMipchain);

	Serializer::StopWriting();

	return true;
}

bool Texture::LoadMetadata()
{
	if (!Serializer::StartReading(GetFilePathMetadata()))
		return false;

	if (Serializer::ReadSTR() == METADATA_TYPE_TEXTURE)
	{
		m_resourceID = Serializer::ReadSTR();
		m_resourceName = Serializer::ReadSTR();
		m_resourceFilePath = Serializer::ReadSTR();
		m_width = Serializer::ReadInt();
		m_height = Serializer::ReadInt();
		m_textureType = TextureType(Serializer::ReadInt());
		m_grayscale = Serializer::ReadBool();
		m_transparency = Serializer::ReadBool();
		m_generateMipchain = Serializer::ReadBool();
	}

	Serializer::StopReading();

	return true;
}

// Loads a texture (not it's metadata) from an image file
bool Texture::LoadFromFile(const string& filePath)
{
	auto graphicsDevice = m_context->GetSubsystem<GraphicsDevice>()->GetDevice();
	if (!graphicsDevice) {
		return false;
	}

	// Load DDS (too bored to implement dds cubemap support in the ImageImporter)
	if (FileSystem::GetExtensionFromPath(filePath) == ".dds")
	{
		ID3D11ShaderResourceView* ddsTex = nullptr;
		wstring widestr = wstring(filePath.begin(), filePath.end());
		HRESULT hr = DirectX::CreateDDSTextureFromFile(graphicsDevice, widestr.c_str(), nullptr, &ddsTex);
		if (FAILED(hr))
		{
			LOG_ERROR("Failed to load texture \"" + filePath + "\".");
			return false;
		}

		m_texture->SetShaderResourceView(ddsTex);
		return true;
	}

	// Load texture
	bool loaded = m_generateMipchain ? ImageImporter::GetInstance().LoadAndCreateMipchain(filePath) : ImageImporter::GetInstance().Load(filePath);
	if (!loaded)
	{
		LOG_ERROR("Failed to load texture \"" + filePath + "\".");
		ImageImporter::GetInstance().Clear();
		return false;
	}

	// Extract any metadata we can from the ImageImporter
	m_resourceFilePath = ImageImporter::GetInstance().GetPath();
	m_resourceName = FileSystem::GetFileNameNoExtensionFromPath(GetFilePathTexture());
	m_width = ImageImporter::GetInstance().GetWidth();
	m_height = ImageImporter::GetInstance().GetHeight();
	m_grayscale = ImageImporter::GetInstance().IsGrayscale();
	m_transparency = ImageImporter::GetInstance().IsTransparent();

	if (!CreateShaderResourceView())
		return false;

	// Free any memory allocated by the ImageImporter
	ImageImporter::GetInstance().Clear();

	if (!LoadMetadata()) // Load metadata file
		if (!SaveMetadata()) // If a metadata file doesn't exist, create one
			return false; // if that failed too, well at least get the file path right mate

	return true;
}

void Texture::SetTextureType(TextureType type)
{
	m_textureType = type;

	// FIX: some models pass a normal map as a height map
	// and others pass a height map as a normal map...
	if (m_textureType == Height && !GetGrayscale())
		m_textureType = Normal;
	if (m_textureType == Normal && GetGrayscale())
		m_textureType = Height;
}

bool Texture::CreateShaderResourceView()
{
	if (!m_context)
		return false;

	if (m_generateMipchain)
	{
		if (!m_texture->CreateFromMipchain(m_width, m_height, ImageImporter::GetInstance().GetChannels(), ImageImporter::GetInstance().GetRGBAMipchain()))
		{
			LOG_ERROR("Failed to create texture from loaded image \"" + ImageImporter::GetInstance().GetPath() + "\".");
			return false;
		}
	}
	else
	{
		if (!m_texture->Create(m_width, m_height, ImageImporter::GetInstance().GetChannels(), ImageImporter::GetInstance().GetRGBA()))
		{
			LOG_ERROR("Failed to create texture from loaded image \"" + ImageImporter::GetInstance().GetPath() + "\".");
			return false;
		}
	}

	return true;
}