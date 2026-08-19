#include "overlay/InternalHudRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace kue::internalhud {
namespace {

constexpr int kMaximumScreenDimension = 16384;
constexpr int kMaximumFontDimension = 8192;
constexpr int kMaximumDrawLists = 16;
constexpr std::size_t kMaximumCommands = 4096;
constexpr std::size_t kMaximumIndices = 262144;
constexpr std::size_t kMaximumVertices = 131072;
constexpr float kMaximumRasterCoordinateMagnitude =
    static_cast<float>(kMaximumScreenDimension) * 2.f;

struct ColorF {
    float red;
    float green;
    float blue;
    float alpha;
};

struct TextureCoordinateInput {
    float normalizedCoordinate;
    int dimension;
};

struct RasterTriangle {
    const ImDrawVert& first;
    const ImDrawVert& second;
    const ImDrawVert& third;
    const ImVec4& clip;
};

template <typename T> bool coherentVectorStorage(const ImVector<T>& vector) noexcept {
    if (vector.Size < 0 || vector.Capacity < 0 || vector.Size > vector.Capacity)
        return false;
    if (vector.Capacity == 0)
        return vector.Data == nullptr;
    return vector.Data != nullptr;
}

bool safeRasterCoordinate(float value) noexcept {
    return std::isfinite(value) && value >= -kMaximumRasterCoordinateMagnitude &&
           value <= kMaximumRasterCoordinateMagnitude;
}

bool sameFloatBits(std::array<float, 2> values) noexcept {
    return std::bit_cast<std::uint32_t>(values[0]) == std::bit_cast<std::uint32_t>(values[1]);
}

MenuRasterStatus calculateRasterBounds(const ui::MenuRectangle& rectangle, MenuScreen screen,
                                       MenuRasterBounds& output) noexcept {
    if (screen.width <= 0 || screen.height <= 0 || screen.width > kMaximumScreenDimension ||
        screen.height > kMaximumScreenDimension || !std::isfinite(rectangle.x) ||
        !std::isfinite(rectangle.y) || !std::isfinite(rectangle.width) ||
        !std::isfinite(rectangle.height) || rectangle.x < 0.f || rectangle.y < 0.f ||
        rectangle.width <= 0.f || rectangle.height <= 0.f ||
        rectangle.x > static_cast<float>(screen.width) ||
        rectangle.y > static_cast<float>(screen.height) ||
        rectangle.width > static_cast<float>(kMaximumScreenDimension) ||
        rectangle.height > static_cast<float>(kMaximumScreenDimension)) {
        return MenuRasterStatus::InvalidOutput;
    }

    const int rectangleX = static_cast<int>(std::floor(rectangle.x));
    const int rectangleY = static_cast<int>(std::floor(rectangle.y));
    const int rectangleWidth = static_cast<int>(std::ceil(rectangle.width));
    const int rectangleHeight = static_cast<int>(std::ceil(rectangle.height));
    MenuRasterBounds calculated{};
    calculated.x = std::clamp(rectangleX - 3, 0, screen.width - 1);
    calculated.y = std::clamp(rectangleY - 3, 0, screen.height - 1);
    calculated.width = std::clamp(rectangleWidth + 6, 1, screen.width - calculated.x);
    calculated.height = std::clamp(rectangleHeight + 6, 1, screen.height - calculated.y);
    if (calculated.width > InternalHudRenderer::kMaximumRasterWidth ||
        calculated.height > InternalHudRenderer::kMaximumRasterHeight) {
        return MenuRasterStatus::InvalidOutput;
    }
    output = calculated;
    return MenuRasterStatus::Success;
}

MenuRasterStatus validateDrawStorage(const ImDrawData* data, const MenuRasterBounds& bounds,
                                     MenuScreen screen, const MenuFontAtlas& font) noexcept {
    if (bounds.x < 0 || bounds.y < 0 || bounds.width <= 0 || bounds.height <= 0 ||
        bounds.width > InternalHudRenderer::kMaximumRasterWidth ||
        bounds.height > InternalHudRenderer::kMaximumRasterHeight ||
        bounds.x > kMaximumScreenDimension - bounds.width ||
        bounds.y > kMaximumScreenDimension - bounds.height) {
        return MenuRasterStatus::InvalidOutput;
    }
    if (font.width <= 0 || font.height <= 0 || font.width > kMaximumFontDimension ||
        font.height > kMaximumFontDimension) {
        return MenuRasterStatus::InvalidFont;
    }
    const std::size_t fontBytes = static_cast<std::size_t>(font.width) *
                                  static_cast<std::size_t>(font.height) * std::size_t{4};
    if (font.pixels.size() != fontBytes)
        return MenuRasterStatus::InvalidFont;
    if (!data || !data->Valid || !coherentVectorStorage(data->CmdLists) ||
        data->CmdListsCount < 0 || data->CmdListsCount != data->CmdLists.Size ||
        data->CmdListsCount > kMaximumDrawLists || data->TotalIdxCount < 0 ||
        data->TotalVtxCount < 0) {
        return MenuRasterStatus::InvalidFrame;
    }
    if (!sameFloatBits({data->DisplayPos.x, 0.f}) || !sameFloatBits({data->DisplayPos.y, 0.f}) ||
        !sameFloatBits({data->DisplaySize.x, static_cast<float>(screen.width)}) ||
        !sameFloatBits({data->DisplaySize.y, static_cast<float>(screen.height)}) ||
        !sameFloatBits({data->FramebufferScale.x, 1.f}) ||
        !sameFloatBits({data->FramebufferScale.y, 1.f})) {
        return MenuRasterStatus::InvalidFrame;
    }

    std::size_t commandCount = 0;
    std::size_t indexCount = 0;
    std::size_t vertexCount = 0;
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* const list = data->CmdLists[listIndex];
        if (!list || !coherentVectorStorage(list->CmdBuffer) ||
            !coherentVectorStorage(list->IdxBuffer) || !coherentVectorStorage(list->VtxBuffer)) {
            return MenuRasterStatus::InvalidList;
        }
        const std::size_t listCommands = static_cast<std::size_t>(list->CmdBuffer.Size);
        const std::size_t listIndices = static_cast<std::size_t>(list->IdxBuffer.Size);
        const std::size_t listVertices = static_cast<std::size_t>(list->VtxBuffer.Size);
        if (listCommands > kMaximumCommands - commandCount ||
            listIndices > kMaximumIndices - indexCount ||
            listVertices > kMaximumVertices - vertexCount) {
            return MenuRasterStatus::CapacityExceeded;
        }
        commandCount += listCommands;
        indexCount += listIndices;
        vertexCount += listVertices;
    }
    if (indexCount != static_cast<std::size_t>(data->TotalIdxCount) ||
        vertexCount != static_cast<std::size_t>(data->TotalVtxCount)) {
        return MenuRasterStatus::InvalidFrame;
    }
    return MenuRasterStatus::Success;
}

MenuRasterStatus validateRasterCommands(const ImDrawData* data) noexcept {
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* const list = data->CmdLists[listIndex];
        const std::size_t listIndices = static_cast<std::size_t>(list->IdxBuffer.Size);
        const std::size_t listVertices = static_cast<std::size_t>(list->VtxBuffer.Size);
        for (std::size_t vertexIndex = 0; vertexIndex < listVertices; ++vertexIndex) {
            const ImDrawVert& vertex = list->VtxBuffer.Data[vertexIndex];
            if (!safeRasterCoordinate(vertex.pos.x) || !safeRasterCoordinate(vertex.pos.y) ||
                !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y) || vertex.uv.x < 0.f ||
                vertex.uv.x > 1.f || vertex.uv.y < 0.f || vertex.uv.y > 1.f) {
                return MenuRasterStatus::InvalidVertex;
            }
        }
        const std::size_t commandCount = static_cast<std::size_t>(list->CmdBuffer.Size);
        for (std::size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const ImDrawCmd& command = list->CmdBuffer.Data[commandIndex];
            if (command.UserCallback)
                return MenuRasterStatus::UnsupportedCallback;
            if (command.GetTexID() != static_cast<ImTextureID>(1))
                return MenuRasterStatus::UnsupportedTexture;
            const std::size_t begin = command.IdxOffset;
            const std::size_t count = command.ElemCount;
            const std::size_t vertexOffset = command.VtxOffset;
            if (count % 3 != 0 || begin > listIndices || count > listIndices - begin ||
                vertexOffset > listVertices || !safeRasterCoordinate(command.ClipRect.x) ||
                !safeRasterCoordinate(command.ClipRect.y) ||
                !safeRasterCoordinate(command.ClipRect.z) ||
                !safeRasterCoordinate(command.ClipRect.w) ||
                command.ClipRect.x > command.ClipRect.z ||
                command.ClipRect.y > command.ClipRect.w) {
                return MenuRasterStatus::InvalidCommand;
            }
            for (std::size_t index = begin; index < begin + count; ++index) {
                const std::size_t vertexIndex = list->IdxBuffer.Data[index];
                if (vertexIndex >= listVertices - vertexOffset)
                    return MenuRasterStatus::InvalidCommand;
            }
        }
    }
    return MenuRasterStatus::Success;
}

ColorF unpack(ImU32 color) noexcept {
    constexpr float scale = 1.f / 255.f;
    return {static_cast<float>(color & 0xffu) * scale,
            static_cast<float>((color >> 8) & 0xffu) * scale,
            static_cast<float>((color >> 16) & 0xffu) * scale,
            static_cast<float>((color >> 24) & 0xffu) * scale};
}

int textureCoordinate(TextureCoordinateInput input) noexcept {
    const float maximum = std::nextafter(1.f, 0.f);
    return static_cast<int>(std::clamp(input.normalizedCoordinate, 0.f, maximum) *
                            static_cast<float>(input.dimension));
}

template <typename AddBytes>
bool visitDrawIdentity(const ImDrawData* data, const MenuRasterBounds& bounds,
                       AddBytes& addBytes) noexcept {
    const int frame[] = {bounds.x,
                         bounds.y,
                         bounds.width,
                         bounds.height,
                         data->CmdListsCount,
                         data->TotalIdxCount,
                         data->TotalVtxCount};
    if (!addBytes(frame, sizeof(frame))) {
        return false;
    }

    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* const list = data->CmdLists.Data[listIndex];
        const int listSizes[] = {
            list->CmdBuffer.Size,
            list->IdxBuffer.Size,
            list->VtxBuffer.Size,
        };
        const std::size_t vertexBytes =
            static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const std::size_t indexBytes =
            static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        if (!addBytes(listSizes, sizeof(listSizes)) ||
            !addBytes(list->VtxBuffer.Data, vertexBytes) ||
            !addBytes(list->IdxBuffer.Data, indexBytes)) {
            return false;
        }

        const std::size_t commandCount = static_cast<std::size_t>(list->CmdBuffer.Size);
        for (std::size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const ImDrawCmd& command = list->CmdBuffer.Data[commandIndex];
            if (!addBytes(&command.ClipRect.x, sizeof(command.ClipRect.x)) ||
                !addBytes(&command.ClipRect.y, sizeof(command.ClipRect.y)) ||
                !addBytes(&command.ClipRect.z, sizeof(command.ClipRect.z)) ||
                !addBytes(&command.ClipRect.w, sizeof(command.ClipRect.w)) ||
                !addBytes(&command.TextureId, sizeof(command.TextureId)) ||
                !addBytes(&command.VtxOffset, sizeof(command.VtxOffset)) ||
                !addBytes(&command.IdxOffset, sizeof(command.IdxOffset)) ||
                !addBytes(&command.ElemCount, sizeof(command.ElemCount)) ||
                !addBytes(static_cast<const void*>(&command.UserCallback),
                          sizeof(command.UserCallback))) {
                return false;
            }
        }
    }
    return true;
}

}

const char* menuRasterStatusName(MenuRasterStatus status) noexcept {
    switch (status) {
    case MenuRasterStatus::Success:
        return "success";
    case MenuRasterStatus::InvalidOutput:
        return "invalid output bounds";
    case MenuRasterStatus::InvalidFont:
        return "invalid font atlas";
    case MenuRasterStatus::InvalidFrame:
        return "invalid draw frame";
    case MenuRasterStatus::CapacityExceeded:
        return "draw capacity exceeded";
    case MenuRasterStatus::InvalidList:
        return "invalid draw list";
    case MenuRasterStatus::UnsupportedCallback:
        return "unsupported draw callback";
    case MenuRasterStatus::UnsupportedTexture:
        return "unsupported draw texture";
    case MenuRasterStatus::InvalidCommand:
        return "invalid draw command";
    case MenuRasterStatus::InvalidVertex:
        return "invalid draw vertex";
    case MenuRasterStatus::RevisionExhausted:
        return "pixel revision exhausted";
    }
    return "invalid raster status";
}

MenuRasterStatus checkedNextPixelRevision(std::uint64_t current, std::uint64_t& output) noexcept {
    if (current == std::numeric_limits<std::uint64_t>::max())
        return MenuRasterStatus::RevisionExhausted;
    output = current + 1;
    return MenuRasterStatus::Success;
}

MenuRasterStatus InternalHudRenderer::setFontAtlas(MenuFontAtlas atlas) noexcept {
    if (atlas.width <= 0 || atlas.height <= 0 || atlas.width > kMaximumFontDimension ||
        atlas.height > kMaximumFontDimension) {
        return MenuRasterStatus::InvalidFont;
    }
    const std::size_t byteCount = static_cast<std::size_t>(atlas.width) *
                                  static_cast<std::size_t>(atlas.height) * std::size_t{4};
    if (atlas.pixels.size() != byteCount)
        return MenuRasterStatus::InvalidFont;
    mFontPixels = atlas.pixels;
    mFontWidth = atlas.width;
    mFontHeight = atlas.height;
    mDrawIdentityValid = false;
    mDrawIdentityBytes = 0;
    return MenuRasterStatus::Success;
}

void InternalHudRenderer::clearFontAtlas() noexcept {
    mFontPixels = {};
    mFontWidth = 0;
    mFontHeight = 0;
    mDrawIdentityValid = false;
    mDrawIdentityBytes = 0;
}

bool InternalHudRenderer::drawIdentityMatches(const ImDrawData* drawData,
                                              const MenuRasterBounds& bounds) const noexcept {
    if (!mDrawIdentityValid)
        return false;
    std::size_t offset = 0;
    bool equal = true;
    auto compareBytes = [&](const void* source, std::size_t byteCount) {
        if (!equal || offset > mDrawIdentityBytes || byteCount > mDrawIdentityBytes - offset) {
            equal = false;
            return false;
        }
        if (byteCount > 0 && std::memcmp(mDrawIdentity.data() + offset, source, byteCount) != 0) {
            equal = false;
            return false;
        }
        offset += byteCount;
        return true;
    };
    return visitDrawIdentity(drawData, bounds, compareBytes) && equal &&
           offset == mDrawIdentityBytes;
}

void InternalHudRenderer::storeDrawIdentity(const ImDrawData* drawData,
                                            const MenuRasterBounds& bounds) noexcept {
    mDrawIdentityValid = false;
    mDrawIdentityBytes = 0;
    auto storeBytes = [&](const void* source, std::size_t byteCount) {
        if (byteCount > mDrawIdentity.size() - mDrawIdentityBytes)
            return false;
        if (byteCount > 0)
            std::memcpy(mDrawIdentity.data() + mDrawIdentityBytes, source, byteCount);
        mDrawIdentityBytes += byteCount;
        return true;
    };
    if (!visitDrawIdentity(drawData, bounds, storeBytes)) {
        mDrawIdentityBytes = 0;
        return;
    }
    mDrawIdentityValid = true;
}

void InternalHudRenderer::rasterValidatedDrawData(const ImDrawData* data,
                                                  const MenuRasterBounds& bounds) noexcept {
    const int cropX = bounds.x;
    const int cropY = bounds.y;
    const int cropW = bounds.width;
    const int cropH = bounds.height;
    const std::size_t byteCount =
        static_cast<std::size_t>(cropW) * static_cast<std::size_t>(cropH) * std::size_t{4};
    std::memset(mPixels.data(), 0, byteCount);
    auto rasterTriangle = [&](RasterTriangle triangle) {
        const ImDrawVert& a = triangle.first;
        const ImDrawVert& b = triangle.second;
        const ImDrawVert& c = triangle.third;
        const ImVec4& clip = triangle.clip;
        const float area =
            (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
        if (!std::isfinite(area) || std::fabs(area) < 0.0001f)
            return;
        const float inverseArea = 1.f / area;
        const float minimumX = std::max(static_cast<float>(cropX),
                                        std::max(clip.x, std::min({a.pos.x, b.pos.x, c.pos.x})));
        const float minimumY = std::max(static_cast<float>(cropY),
                                        std::max(clip.y, std::min({a.pos.y, b.pos.y, c.pos.y})));
        const float maximumX = std::min(static_cast<float>(cropX + cropW - 1),
                                        std::min(clip.z, std::max({a.pos.x, b.pos.x, c.pos.x})));
        const float maximumY = std::min(static_cast<float>(cropY + cropH - 1),
                                        std::min(clip.w, std::max({a.pos.y, b.pos.y, c.pos.y})));
        const int x0 = static_cast<int>(std::floor(minimumX));
        const int y0 = static_cast<int>(std::floor(minimumY));
        const int x1 = static_cast<int>(std::ceil(maximumX));
        const int y1 = static_cast<int>(std::ceil(maximumY));
        if (x1 < x0 || y1 < y0)
            return;

        const ColorF colorA = unpack(a.col);
        const ColorF colorB = unpack(b.col);
        const ColorF colorC = unpack(c.col);
        const bool solid = a.col == b.col && a.col == c.col && sameFloatBits({a.uv.x, b.uv.x}) &&
                           sameFloatBits({a.uv.y, b.uv.y}) && sameFloatBits({a.uv.x, c.uv.x}) &&
                           sameFloatBits({a.uv.y, c.uv.y});
        float solidRed = 0.f;
        float solidGreen = 0.f;
        float solidBlue = 0.f;
        float solidAlpha = 0.f;
        if (solid) {
            const int textureX =
                textureCoordinate({.normalizedCoordinate = a.uv.x, .dimension = mFontWidth});
            const int textureY =
                textureCoordinate({.normalizedCoordinate = a.uv.y, .dimension = mFontHeight});
            const std::size_t textureOffset =
                (static_cast<std::size_t>(textureY) * static_cast<std::size_t>(mFontWidth) +
                 static_cast<std::size_t>(textureX)) *
                std::size_t{4};
            const std::uint8_t* texture = mFontPixels.data() + textureOffset;
            constexpr float scale = 1.f / 255.f;
            solidRed = colorA.red * static_cast<float>(texture[0]) * scale;
            solidGreen = colorA.green * static_cast<float>(texture[1]) * scale;
            solidBlue = colorA.blue * static_cast<float>(texture[2]) * scale;
            solidAlpha = colorA.alpha * static_cast<float>(texture[3]) * scale;
        }

        const float weight0StepX = (b.pos.y - c.pos.y) * inverseArea;
        const float weight0StepY = (c.pos.x - b.pos.x) * inverseArea;
        const float weight1StepX = (c.pos.y - a.pos.y) * inverseArea;
        const float weight1StepY = (a.pos.x - c.pos.x) * inverseArea;
        const float firstX = static_cast<float>(x0) + 0.5f;
        const float firstY = static_cast<float>(y0) + 0.5f;
        float rowWeight0 =
            ((b.pos.x - firstX) * (c.pos.y - firstY) - (b.pos.y - firstY) * (c.pos.x - firstX)) *
            inverseArea;
        float rowWeight1 =
            ((c.pos.x - firstX) * (a.pos.y - firstY) - (c.pos.y - firstY) * (a.pos.x - firstX)) *
            inverseArea;
        for (int y = y0; y <= y1; ++y) {
            float weight0 = rowWeight0;
            float weight1 = rowWeight1;
            for (int x = x0; x <= x1; ++x) {
                const float weight2 = 1.f - weight0 - weight1;
                if (weight0 < -0.0001f || weight1 < -0.0001f || weight2 < -0.0001f) {
                    weight0 += weight0StepX;
                    weight1 += weight1StepX;
                    continue;
                }

                float sourceRed;
                float sourceGreen;
                float sourceBlue;
                float sourceAlpha;
                if (solid) {
                    sourceRed = solidRed;
                    sourceGreen = solidGreen;
                    sourceBlue = solidBlue;
                    sourceAlpha = solidAlpha;
                } else {
                    const float textureU = a.uv.x * weight0 + b.uv.x * weight1 + c.uv.x * weight2;
                    const float textureV = a.uv.y * weight0 + b.uv.y * weight1 + c.uv.y * weight2;
                    const int textureX = textureCoordinate(
                        {.normalizedCoordinate = textureU, .dimension = mFontWidth});
                    const int textureY = textureCoordinate(
                        {.normalizedCoordinate = textureV, .dimension = mFontHeight});
                    const std::size_t textureOffset =
                        (static_cast<std::size_t>(textureY) * static_cast<std::size_t>(mFontWidth) +
                         static_cast<std::size_t>(textureX)) *
                        std::size_t{4};
                    const std::uint8_t* texture = mFontPixels.data() + textureOffset;
                    constexpr float scale = 1.f / 255.f;
                    sourceRed =
                        (colorA.red * weight0 + colorB.red * weight1 + colorC.red * weight2) *
                        static_cast<float>(texture[0]) * scale;
                    sourceGreen =
                        (colorA.green * weight0 + colorB.green * weight1 + colorC.green * weight2) *
                        static_cast<float>(texture[1]) * scale;
                    sourceBlue =
                        (colorA.blue * weight0 + colorB.blue * weight1 + colorC.blue * weight2) *
                        static_cast<float>(texture[2]) * scale;
                    sourceAlpha =
                        (colorA.alpha * weight0 + colorB.alpha * weight1 + colorC.alpha * weight2) *
                        static_cast<float>(texture[3]) * scale;
                }
                if (sourceAlpha <= 0.001f) {
                    weight0 += weight0StepX;
                    weight1 += weight1StepX;
                    continue;
                }

                const int localX = x - cropX;
                const int localY = cropH - 1 - (y - cropY);
                const std::size_t pixelOffset =
                    (static_cast<std::size_t>(localY) * static_cast<std::size_t>(cropW) +
                     static_cast<std::size_t>(localX)) *
                    std::size_t{4};
                std::uint8_t* destination = mPixels.data() + pixelOffset;
                if (sourceAlpha >= 0.9999f) {
                    destination[0] =
                        static_cast<std::uint8_t>(std::clamp(sourceRed, 0.f, 1.f) * 255.f);
                    destination[1] =
                        static_cast<std::uint8_t>(std::clamp(sourceGreen, 0.f, 1.f) * 255.f);
                    destination[2] =
                        static_cast<std::uint8_t>(std::clamp(sourceBlue, 0.f, 1.f) * 255.f);
                    destination[3] = 255;
                    weight0 += weight0StepX;
                    weight1 += weight1StepX;
                    continue;
                }
                constexpr float byteScale = 1.f / 255.f;
                const float destinationRed = static_cast<float>(destination[0]) * byteScale;
                const float destinationGreen = static_cast<float>(destination[1]) * byteScale;
                const float destinationBlue = static_cast<float>(destination[2]) * byteScale;
                const float destinationAlpha = static_cast<float>(destination[3]) * byteScale;
                const float outputAlpha = sourceAlpha + destinationAlpha * (1.f - sourceAlpha);
                if (outputAlpha <= 0.f) {
                    weight0 += weight0StepX;
                    weight1 += weight1StepX;
                    continue;
                }
                destination[0] = static_cast<std::uint8_t>(
                    std::clamp((sourceRed * sourceAlpha +
                                destinationRed * destinationAlpha * (1.f - sourceAlpha)) /
                                   outputAlpha,
                               0.f, 1.f) *
                    255.f);
                destination[1] = static_cast<std::uint8_t>(
                    std::clamp((sourceGreen * sourceAlpha +
                                destinationGreen * destinationAlpha * (1.f - sourceAlpha)) /
                                   outputAlpha,
                               0.f, 1.f) *
                    255.f);
                destination[2] = static_cast<std::uint8_t>(
                    std::clamp((sourceBlue * sourceAlpha +
                                destinationBlue * destinationAlpha * (1.f - sourceAlpha)) /
                                   outputAlpha,
                               0.f, 1.f) *
                    255.f);
                destination[3] =
                    static_cast<std::uint8_t>(std::clamp(outputAlpha, 0.f, 1.f) * 255.f);
                weight0 += weight0StepX;
                weight1 += weight1StepX;
            }
            rowWeight0 += weight0StepY;
            rowWeight1 += weight1StepY;
        }
    };

    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* const list = data->CmdLists[listIndex];
        const std::size_t commandCount = static_cast<std::size_t>(list->CmdBuffer.Size);
        for (std::size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const ImDrawCmd& command = list->CmdBuffer.Data[commandIndex];
            const std::size_t begin = command.IdxOffset;
            const std::size_t end = begin + command.ElemCount;
            const std::size_t vertexOffset = command.VtxOffset;
            for (std::size_t index = begin; index + 2 < end; index += 3) {
                const std::size_t first = list->IdxBuffer.Data[index];
                const std::size_t second = list->IdxBuffer.Data[index + 1];
                const std::size_t third = list->IdxBuffer.Data[index + 2];
                rasterTriangle({.first = list->VtxBuffer.Data[vertexOffset + first],
                                .second = list->VtxBuffer.Data[vertexOffset + second],
                                .third = list->VtxBuffer.Data[vertexOffset + third],
                                .clip = command.ClipRect});
            }
        }
    }
}

MenuRasterStatus InternalHudRenderer::render(const ImDrawData* drawData,
                                             const ui::MenuRectangle& rectangle, MenuScreen screen,
                                             MenuRasterFrame& output) noexcept {
    MenuRasterBounds bounds{};
    MenuRasterStatus status = calculateRasterBounds(rectangle, screen, bounds);
    if (status != MenuRasterStatus::Success)
        return status;
    const MenuFontAtlas font{mFontPixels, mFontWidth, mFontHeight};
    status = validateDrawStorage(drawData, bounds, screen, font);
    if (status != MenuRasterStatus::Success)
        return status;

    const bool pixelsChanged = mPixelBytes == 0 || !drawIdentityMatches(drawData, bounds);
    if (pixelsChanged) {
        status = validateRasterCommands(drawData);
        if (status != MenuRasterStatus::Success)
            return status;
        std::uint64_t nextRevision = 0;
        status = checkedNextPixelRevision(mPixelRevision, nextRevision);
        if (status != MenuRasterStatus::Success)
            return status;
        rasterValidatedDrawData(drawData, bounds);
        mPixelBytes = static_cast<std::size_t>(bounds.width) *
                      static_cast<std::size_t>(bounds.height) * std::size_t{4};
        storeDrawIdentity(drawData, bounds);
        mPixelRevision = nextRevision;
    }

    output = {std::span<const std::uint8_t>{mPixels.data(), mPixelBytes},
              bounds.x,
              bounds.y,
              bounds.width,
              bounds.height,
              mPixelRevision,
              pixelsChanged ? MenuPixelChange::Changed : MenuPixelChange::Unchanged};
    return MenuRasterStatus::Success;
}

InternalHudFrameTransaction::InternalHudFrameTransaction(const ui::Interface& interface,
                                                         const Config& config) noexcept
    : mInterface(interface), mConfiguration(ui::menuConfiguration(config)) {}

ui::Interface& InternalHudFrameTransaction::interface() noexcept {
    return mInterface;
}

ui::MenuConfiguration& InternalHudFrameTransaction::configuration() noexcept {
    return mConfiguration;
}

MenuRasterStatus InternalHudFrameTransaction::commit(
    InternalHudRenderer& renderer, const ImDrawData* drawData, const ui::MenuRectangle& rectangle,
    MenuScreen screen, ui::Interface& liveInterface, Config& liveConfig,
    MenuRasterFrame& rasterFrame, MenuFrameEffects& effects) noexcept {
    const MenuRasterStatus status = renderer.render(drawData, rectangle, screen, rasterFrame);
    if (status != MenuRasterStatus::Success)
        return status;

    MenuFrameEffects acceptedEffects{mInterface.consumeConfigChange(),
                                     mInterface.consumeSaveRequest()};
    liveInterface = mInterface;
    if (acceptedEffects.configurationChanged)
        ui::applyMenuConfiguration(liveConfig, mConfiguration);
    effects = acceptedEffects;
    return MenuRasterStatus::Success;
}

}
