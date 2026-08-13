// Migrated: Intent/Bundle/Toast/SMS/SAX/MotionEvent/misc handler bodies now
// live in their per-class declaration files (android_os_Bundle.cpp,
// android_content_Intent.cpp, android_content_IntentFilter.cpp,
// android_view_MotionEvent.cpp, android_util_Pair.cpp,
// android_app_PendingIntent.cpp, android_content_BroadcastReceiver.cpp,
// javax_xml_parsers_SAXParserFactory.cpp, javax_xml_parsers_SAXParser.cpp,
// android_telephony_SmsMessage.cpp, android_telephony_SmsManager.cpp,
// android_widget_Toast.cpp, android_net_Uri.cpp). This empty batch keeps
// the assembly linking until the AndroidHandlers scaffold is removed.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateMisc(AndroidHandlers& handlers, const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

}  // namespace ogplay::runtime::android_intrinsics
