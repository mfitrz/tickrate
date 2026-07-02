#pragma once
#include <QString>

// Wear-condition domain constants shared across UI and network layers.
static constexpr int kWearCount = 5;
static const char *const kWearSuffix[kWearCount] = {
    "(Factory New)", "(Minimal Wear)", "(Field-Tested)", "(Well-Worn)", "(Battle-Scarred)"
};
static const char *const kWearShort[kWearCount] = { "FN", "MW", "FT", "WW", "BS" };

struct CS2Listing {
    QString listingId;          // CSFloat numeric ID — used to build the listing URL
    double  price      = 0.0;
    double  floatValue = 0.0;
    QString wear;               // "FN", "MW", "FT", "WW", "BS"
};
