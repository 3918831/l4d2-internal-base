#pragma once

#include <cstdint>

enum class PortalBrushAccessStatus
{
    Success,
    MissingData,
    GenerationMismatch,
    InvalidBrushIndex,
    AddressOverflow,
    Unreadable,
    Unwritable,
    UnexpectedContents,
    WriteFailed,
    VerificationFailed
};

struct PortalBrushReadResult
{
    PortalBrushAccessStatus status = PortalBrushAccessStatus::MissingData;
    int contents = 0;

    bool Succeeded() const
    {
        return status == PortalBrushAccessStatus::Success;
    }
};

struct PortalBrushWriteResult
{
    PortalBrushAccessStatus status = PortalBrushAccessStatus::MissingData;
    int previousContents = 0;
    int currentContents = 0;

    bool Succeeded() const
    {
        return status == PortalBrushAccessStatus::Success;
    }
};

class IPortalBspBrushAccess
{
public:
    virtual ~IPortalBspBrushAccess() = default;

    virtual std::uint32_t GetMapGeneration() const = 0;
    virtual PortalBrushReadResult ReadBrushContents(
        int brushIndex,
        std::uint32_t expectedGeneration) const = 0;
    virtual PortalBrushWriteResult CompareAndWriteBrushContents(
        int brushIndex,
        std::uint32_t expectedGeneration,
        int expectedCurrent,
        int replacement) = 0;
};

const char* PortalBrushAccessStatusName(PortalBrushAccessStatus status);
