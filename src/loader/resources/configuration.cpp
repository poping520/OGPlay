// Copyright (C) 2008 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// Adapted from android-4.4.4_r2.0.1 frameworks/base/libs/androidfw/ResourceTypes.cpp
// and core/jni/android_util_AssetManager.cpp. See MODULE.md for correspondence.
#include "binary.h"

namespace ogplay::loader::resources {
bool ResTable_config::isMoreSpecificThan(const ResTable_config& o) const {
    // The order of the following tests defines the importance of one
    // configuration parameter over another.  Those tests first are more
    // important, trumping any values in those following them.
    if ((mcc || mnc) || (o.mcc || o.mnc)) {
        if (mcc != o.mcc) {
            if (!mcc) return false;
            if (!o.mcc) return true;
        }

        if (mnc != o.mnc) {
            if (!mnc) return false;
            if (!o.mnc) return true;
        }
    }

    if ((language[0] || country[0]) || (o.language[0] || o.country[0])) {
        if (language[0] != o.language[0]) {
            if (!language[0]) return false;
            if (!o.language[0]) return true;
        }

        if (country[0] != o.country[0]) {
            if (!country[0]) return false;
            if (!o.country[0]) return true;
        }
    }

    if (screenLayout || o.screenLayout) {
        if (((screenLayout ^ o.screenLayout) & MASK_LAYOUTDIR) != 0) {
            if (!(screenLayout & MASK_LAYOUTDIR)) return false;
            if (!(o.screenLayout & MASK_LAYOUTDIR)) return true;
        }
    }

    if (smallestScreenWidthDp || o.smallestScreenWidthDp) {
        if (smallestScreenWidthDp != o.smallestScreenWidthDp) {
            if (!smallestScreenWidthDp) return false;
            if (!o.smallestScreenWidthDp) return true;
        }
    }

    if ((screenWidthDp || screenHeightDp) || (o.screenWidthDp || o.screenHeightDp)) {
        if (screenWidthDp != o.screenWidthDp) {
            if (!screenWidthDp) return false;
            if (!o.screenWidthDp) return true;
        }

        if (screenHeightDp != o.screenHeightDp) {
            if (!screenHeightDp) return false;
            if (!o.screenHeightDp) return true;
        }
    }

    if (screenLayout || o.screenLayout) {
        if (((screenLayout ^ o.screenLayout) & MASK_SCREENSIZE) != 0) {
            if (!(screenLayout & MASK_SCREENSIZE)) return false;
            if (!(o.screenLayout & MASK_SCREENSIZE)) return true;
        }
        if (((screenLayout ^ o.screenLayout) & MASK_SCREENLONG) != 0) {
            if (!(screenLayout & MASK_SCREENLONG)) return false;
            if (!(o.screenLayout & MASK_SCREENLONG)) return true;
        }
    }

    if (orientation != o.orientation) {
        if (!orientation) return false;
        if (!o.orientation) return true;
    }

    if (uiMode || o.uiMode) {
        if (((uiMode ^ o.uiMode) & MASK_UI_MODE_TYPE) != 0) {
            if (!(uiMode & MASK_UI_MODE_TYPE)) return false;
            if (!(o.uiMode & MASK_UI_MODE_TYPE)) return true;
        }
        if (((uiMode ^ o.uiMode) & MASK_UI_MODE_NIGHT) != 0) {
            if (!(uiMode & MASK_UI_MODE_NIGHT)) return false;
            if (!(o.uiMode & MASK_UI_MODE_NIGHT)) return true;
        }
    }

    // density is never 'more specific'
    // as the default just equals 160

    if (touchscreen != o.touchscreen) {
        if (!touchscreen) return false;
        if (!o.touchscreen) return true;
    }

    if ((keyboard || navigation || inputFlags) || (o.keyboard || o.navigation || o.inputFlags)) {
        if (((inputFlags ^ o.inputFlags) & MASK_KEYSHIDDEN) != 0) {
            if (!(inputFlags & MASK_KEYSHIDDEN)) return false;
            if (!(o.inputFlags & MASK_KEYSHIDDEN)) return true;
        }

        if (((inputFlags ^ o.inputFlags) & MASK_NAVHIDDEN) != 0) {
            if (!(inputFlags & MASK_NAVHIDDEN)) return false;
            if (!(o.inputFlags & MASK_NAVHIDDEN)) return true;
        }

        if (keyboard != o.keyboard) {
            if (!keyboard) return false;
            if (!o.keyboard) return true;
        }

        if (navigation != o.navigation) {
            if (!navigation) return false;
            if (!o.navigation) return true;
        }
    }

    if ((screenWidth || screenHeight) || (o.screenWidth || o.screenHeight)) {
        if (screenWidth != o.screenWidth) {
            if (!screenWidth) return false;
            if (!o.screenWidth) return true;
        }

        if (screenHeight != o.screenHeight) {
            if (!screenHeight) return false;
            if (!o.screenHeight) return true;
        }
    }

    if ((sdkVersion || minorVersion) || (o.sdkVersion || o.minorVersion)) {
        if (sdkVersion != o.sdkVersion) {
            if (!sdkVersion) return false;
            if (!o.sdkVersion) return true;
        }

        if (minorVersion != o.minorVersion) {
            if (!minorVersion) return false;
            if (!o.minorVersion) return true;
        }
    }
    return false;
}

bool ResTable_config::isBetterThan(const ResTable_config& o, const ResTable_config* requested) const {
    if (requested) {
        if ((mcc || mnc) || (o.mcc || o.mnc)) {
            if ((mcc != o.mcc) && requested->mcc) {
                return (mcc);
            }

            if ((mnc != o.mnc) && requested->mnc) {
                return (mnc);
            }
        }

        if ((language[0] || country[0]) || (o.language[0] || o.country[0])) {
            if ((language[0] != o.language[0]) && requested->language[0]) {
                return (language[0]);
            }

            if ((country[0] != o.country[0]) && requested->country[0]) {
                return (country[0]);
            }
        }

        if (screenLayout || o.screenLayout) {
            if (((screenLayout ^ o.screenLayout) & MASK_LAYOUTDIR) != 0 && (requested->screenLayout & MASK_LAYOUTDIR)) {
                int myLayoutDir = screenLayout & MASK_LAYOUTDIR;
                int oLayoutDir = o.screenLayout & MASK_LAYOUTDIR;
                return (myLayoutDir > oLayoutDir);
            }
        }

        if (smallestScreenWidthDp || o.smallestScreenWidthDp) {
            // The configuration closest to the actual size is best.
            // We assume that larger configs have already been filtered
            // out at this point.  That means we just want the largest one.
            if (smallestScreenWidthDp != o.smallestScreenWidthDp) {
                return smallestScreenWidthDp > o.smallestScreenWidthDp;
            }
        }

        if ((screenWidthDp || screenHeightDp) || (o.screenWidthDp || o.screenHeightDp)) {
            // "Better" is based on the sum of the difference between both
            // width and height from the requested dimensions.  We are
            // assuming the invalid configs (with smaller dimens) have
            // already been filtered.  Note that if a particular dimension
            // is unspecified, we will end up with a large value (the
            // difference between 0 and the requested dimension), which is
            // good since we will prefer a config that has specified a
            // dimension value.
            int myDelta = 0, otherDelta = 0;
            if (requested->screenWidthDp) {
                myDelta += requested->screenWidthDp - screenWidthDp;
                otherDelta += requested->screenWidthDp - o.screenWidthDp;
            }
            if (requested->screenHeightDp) {
                myDelta += requested->screenHeightDp - screenHeightDp;
                otherDelta += requested->screenHeightDp - o.screenHeightDp;
            }
            if (myDelta != otherDelta) {
                return myDelta < otherDelta;
            }
        }

        if (screenLayout || o.screenLayout) {
            if (((screenLayout ^ o.screenLayout) & MASK_SCREENSIZE) != 0 &&
                (requested->screenLayout & MASK_SCREENSIZE)) {
                // A little backwards compatibility here: undefined is
                // considered equivalent to normal.  But only if the
                // requested size is at least normal; otherwise, small
                // is better than the default.
                int mySL = (screenLayout & MASK_SCREENSIZE);
                int oSL = (o.screenLayout & MASK_SCREENSIZE);
                int fixedMySL = mySL;
                int fixedOSL = oSL;
                if ((requested->screenLayout & MASK_SCREENSIZE) >= SCREENSIZE_NORMAL) {
                    if (fixedMySL == 0) fixedMySL = SCREENSIZE_NORMAL;
                    if (fixedOSL == 0) fixedOSL = SCREENSIZE_NORMAL;
                }
                // For screen size, the best match is the one that is
                // closest to the requested screen size, but not over
                // (the not over part is dealt with in match() below).
                if (fixedMySL == fixedOSL) {
                    // If the two are the same, but 'this' is actually
                    // undefined, then the other is really a better match.
                    if (mySL == 0) return false;
                    return true;
                }
                if (fixedMySL != fixedOSL) {
                    return fixedMySL > fixedOSL;
                }
            }
            if (((screenLayout ^ o.screenLayout) & MASK_SCREENLONG) != 0 &&
                (requested->screenLayout & MASK_SCREENLONG)) {
                return (screenLayout & MASK_SCREENLONG);
            }
        }

        if ((orientation != o.orientation) && requested->orientation) {
            return (orientation);
        }

        if (uiMode || o.uiMode) {
            if (((uiMode ^ o.uiMode) & MASK_UI_MODE_TYPE) != 0 && (requested->uiMode & MASK_UI_MODE_TYPE)) {
                return (uiMode & MASK_UI_MODE_TYPE);
            }
            if (((uiMode ^ o.uiMode) & MASK_UI_MODE_NIGHT) != 0 && (requested->uiMode & MASK_UI_MODE_NIGHT)) {
                return (uiMode & MASK_UI_MODE_NIGHT);
            }
        }

        if ((orientation || touchscreen || density) || (o.orientation || o.touchscreen || o.density)) {
            if (density != o.density) {
                // density is tough.  Any density is potentially useful
                // because the system will scale it.  Scaling down
                // is generally better than scaling up.
                // Default density counts as 160dpi (the system default)
                // TODO - remove 160 constants
                int h = (density ? density : 160);
                int l = (o.density ? o.density : 160);
                bool bImBigger = true;
                if (l > h) {
                    int t = h;
                    h = l;
                    l = t;
                    bImBigger = false;
                }

                int reqValue = (requested->density ? requested->density : 160);
                if (reqValue >= h) {
                    // requested value higher than both l and h, give h
                    return bImBigger;
                }
                if (l >= reqValue) {
                    // requested value lower than both l and h, give l
                    return !bImBigger;
                }
                // saying that scaling down is 2x better than up
                if (((2LL * l) - reqValue) * h > static_cast<std::int64_t>(reqValue) * reqValue) {
                    return !bImBigger;
                } else {
                    return bImBigger;
                }
            }

            if ((touchscreen != o.touchscreen) && requested->touchscreen) {
                return (touchscreen);
            }
        }

        if ((keyboard || navigation || inputFlags) || (o.keyboard || o.navigation || o.inputFlags)) {
            const int keysHidden = inputFlags & MASK_KEYSHIDDEN;
            const int oKeysHidden = o.inputFlags & MASK_KEYSHIDDEN;
            if (keysHidden != oKeysHidden) {
                const int reqKeysHidden = requested->inputFlags & MASK_KEYSHIDDEN;
                if (reqKeysHidden) {
                    if (!keysHidden) return false;
                    if (!oKeysHidden) return true;
                    // For compatibility, we count KEYSHIDDEN_NO as being
                    // the same as KEYSHIDDEN_SOFT.  Here we disambiguate
                    // these by making an exact match more specific.
                    if (reqKeysHidden == keysHidden) return true;
                    if (reqKeysHidden == oKeysHidden) return false;
                }
            }

            const int navHidden = inputFlags & MASK_NAVHIDDEN;
            const int oNavHidden = o.inputFlags & MASK_NAVHIDDEN;
            if (navHidden != oNavHidden) {
                const int reqNavHidden = requested->inputFlags & MASK_NAVHIDDEN;
                if (reqNavHidden) {
                    if (!navHidden) return false;
                    if (!oNavHidden) return true;
                }
            }

            if ((keyboard != o.keyboard) && requested->keyboard) {
                return (keyboard);
            }

            if ((navigation != o.navigation) && requested->navigation) {
                return (navigation);
            }
        }

        if ((screenWidth || screenHeight) || (o.screenWidth || o.screenHeight)) {
            // "Better" is based on the sum of the difference between both
            // width and height from the requested dimensions.  We are
            // assuming the invalid configs (with smaller sizes) have
            // already been filtered.  Note that if a particular dimension
            // is unspecified, we will end up with a large value (the
            // difference between 0 and the requested dimension), which is
            // good since we will prefer a config that has specified a
            // size value.
            int myDelta = 0, otherDelta = 0;
            if (requested->screenWidth) {
                myDelta += requested->screenWidth - screenWidth;
                otherDelta += requested->screenWidth - o.screenWidth;
            }
            if (requested->screenHeight) {
                myDelta += requested->screenHeight - screenHeight;
                otherDelta += requested->screenHeight - o.screenHeight;
            }
            if (myDelta != otherDelta) {
                return myDelta < otherDelta;
            }
        }

        if ((sdkVersion || minorVersion) || (o.sdkVersion || o.minorVersion)) {
            if ((sdkVersion != o.sdkVersion) && requested->sdkVersion) {
                return (sdkVersion > o.sdkVersion);
            }

            if ((minorVersion != o.minorVersion) && requested->minorVersion) {
                return (minorVersion);
            }
        }

        return false;
    }
    return isMoreSpecificThan(o);
}

bool ResTable_config::match(const ResTable_config& settings) const {
    if ((mcc || mnc) != 0) {
        if (mcc != 0 && mcc != settings.mcc) {
            return false;
        }
        if (mnc != 0 && mnc != settings.mnc) {
            return false;
        }
    }
    if ((language[0] || country[0]) != 0) {
        if (language[0] != 0 && (language[0] != settings.language[0] || language[1] != settings.language[1])) {
            return false;
        }
        if (country[0] != 0 && (country[0] != settings.country[0] || country[1] != settings.country[1])) {
            return false;
        }
    }
    if ((screenLayout || uiMode || smallestScreenWidthDp) != 0) {
        const int layoutDir = screenLayout & MASK_LAYOUTDIR;
        const int setLayoutDir = settings.screenLayout & MASK_LAYOUTDIR;
        if (layoutDir != 0 && layoutDir != setLayoutDir) {
            return false;
        }

        const int sizeClass = screenLayout & MASK_SCREENSIZE;
        const int setScreenSize = settings.screenLayout & MASK_SCREENSIZE;
        // Any screen sizes for larger screens than the setting do not
        // match.
        if (sizeClass != 0 && sizeClass > setScreenSize) {
            return false;
        }

        const int screenLong = screenLayout & MASK_SCREENLONG;
        const int setScreenLong = settings.screenLayout & MASK_SCREENLONG;
        if (screenLong != 0 && screenLong != setScreenLong) {
            return false;
        }

        const int uiModeType = uiMode & MASK_UI_MODE_TYPE;
        const int setUiModeType = settings.uiMode & MASK_UI_MODE_TYPE;
        if (uiModeType != 0 && uiModeType != setUiModeType) {
            return false;
        }

        const int uiModeNight = uiMode & MASK_UI_MODE_NIGHT;
        const int setUiModeNight = settings.uiMode & MASK_UI_MODE_NIGHT;
        if (uiModeNight != 0 && uiModeNight != setUiModeNight) {
            return false;
        }

        if (smallestScreenWidthDp != 0 && smallestScreenWidthDp > settings.smallestScreenWidthDp) {
            return false;
        }
    }
    if ((screenWidthDp || screenHeightDp) != 0) {
        if (screenWidthDp != 0 && screenWidthDp > settings.screenWidthDp) {
            return false;
        }
        if (screenHeightDp != 0 && screenHeightDp > settings.screenHeightDp) {
            return false;
        }
    }
    if ((orientation || touchscreen || density) != 0) {
        if (orientation != 0 && orientation != settings.orientation) {
            return false;
        }
        // density always matches - we can scale it.  See isBetterThan
        if (touchscreen != 0 && touchscreen != settings.touchscreen) {
            return false;
        }
    }
    if ((keyboard || navigation || inputFlags) != 0) {
        const int keysHidden = inputFlags & MASK_KEYSHIDDEN;
        const int setKeysHidden = settings.inputFlags & MASK_KEYSHIDDEN;
        if (keysHidden != 0 && keysHidden != setKeysHidden) {
            // For compatibility, we count a request for KEYSHIDDEN_NO as also
            // matching the more recent KEYSHIDDEN_SOFT.  Basically
            // KEYSHIDDEN_NO means there is some kind of keyboard available.
            if (keysHidden != KEYSHIDDEN_NO || setKeysHidden != KEYSHIDDEN_SOFT) {
                return false;
            }
        }
        const int navHidden = inputFlags & MASK_NAVHIDDEN;
        const int setNavHidden = settings.inputFlags & MASK_NAVHIDDEN;
        if (navHidden != 0 && navHidden != setNavHidden) {
            return false;
        }
        if (keyboard != 0 && keyboard != settings.keyboard) {
            return false;
        }
        if (navigation != 0 && navigation != settings.navigation) {
            return false;
        }
    }
    if ((screenWidth || screenHeight) != 0) {
        if (screenWidth != 0 && screenWidth > settings.screenWidth) {
            return false;
        }
        if (screenHeight != 0 && screenHeight > settings.screenHeight) {
            return false;
        }
    }
    if ((sdkVersion || minorVersion) != 0) {
        if (sdkVersion != 0 && sdkVersion > settings.sdkVersion) {
            return false;
        }
        if (minorVersion != 0 && minorVersion != settings.minorVersion) {
            return false;
        }
    }
    return true;
}

ResTable_config ResTable_config::decode(Bytes b) {
    using namespace detail;
    auto size = u32(b, 0);
    require(size >= 28 && size <= b.size(), "invalid resource configuration");
    ResTable_config c;
    c.mcc = u16(b, 4);
    c.mnc = u16(b, 6);
    c.language[0] = static_cast<char>(u8(b, 8));
    c.language[1] = static_cast<char>(u8(b, 9));
    c.country[0] = static_cast<char>(u8(b, 10));
    c.country[1] = static_cast<char>(u8(b, 11));
    c.orientation = u8(b, 12);
    c.touchscreen = u8(b, 13);
    c.density = u16(b, 14);
    c.keyboard = u8(b, 16);
    c.navigation = u8(b, 17);
    c.inputFlags = u8(b, 18);
    c.screenWidth = u16(b, 20);
    c.screenHeight = u16(b, 22);
    c.sdkVersion = u16(b, 24);
    c.minorVersion = u16(b, 26);
    if (size >= 32) {
        c.screenLayout = u8(b, 28);
        c.uiMode = u8(b, 29);
        c.smallestScreenWidthDp = u16(b, 30);
    }
    if (size >= 36) {
        c.screenWidthDp = u16(b, 32);
        c.screenHeightDp = u16(b, 34);
    }
    return c;
}

std::string ResTable_config::localeName() const {
    if (!language[0]) return {};
    std::string s(language, 2);
    if (country[0]) {
        s += '_';
        s.append(country, 2);
    }
    return s;
}
}  // namespace ogplay::loader::resources
