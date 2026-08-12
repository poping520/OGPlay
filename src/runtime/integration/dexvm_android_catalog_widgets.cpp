// Catalog batch: the state-holding widget layer, dialogs, WebView and
// the hierarchy placeholders that only linking needs.

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

void AppendWidgetClasses(std::vector<Decl>& catalog) {
    {
        // Installer widget layer. The dex_activity lifecycle never draws
        // the view hierarchy (the GL surface is the only visual output),
        // so widgets are state-holding views: text is real (backed by the
        // interpreter's builder buffers so game logic round-trips), all
        // presentation setters are truthful no-ops, geometry queries
        // answer the real surface.
        Decl view_group;
        view_group.descriptor = "Landroid/view/ViewGroup;";
        view_group.superclass = "Landroid/view/View;";
        view_group.methods = {
            {"addView", "(Landroid/view/View;)V", false, false,
             "android.widget.noop"},
            {"addView",
             "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
            {"addView",
             "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
            {"removeView", "(Landroid/view/View;)V", false, false,
             "android.widget.noop"},
            {"removeViews", "(II)V", false, false, "android.widget.noop"},
            {"updateViewLayout",
             "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(view_group));
        Decl layout_params_base;
        layout_params_base.descriptor =
            "Landroid/view/ViewGroup$LayoutParams;";
        layout_params_base.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(layout_params_base));
        Decl absolute_layout;
        absolute_layout.descriptor = "Landroid/widget/AbsoluteLayout;";
        absolute_layout.superclass = "Landroid/view/ViewGroup;";
        absolute_layout.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(absolute_layout));
        // Container family used by the titles' inflated layouts.
        for (const char* descriptor :
             {"Landroid/widget/LinearLayout;",
              "Landroid/widget/FrameLayout;",
              "Landroid/widget/RelativeLayout;",
              "Landroid/widget/TableLayout;", "Landroid/widget/TableRow;",
              "Landroid/widget/ScrollView;"}) {
            Decl container;
            container.descriptor = descriptor;
            container.superclass = "Landroid/view/ViewGroup;";
            container.methods = {
                {"<init>", "(Landroid/content/Context;)V", false, false,
                 "android.view.init"},
            };
            catalog.push_back(std::move(container));
        }
        Decl text_view;
        text_view.descriptor = "Landroid/widget/TextView;";
        text_view.superclass = "Landroid/view/View;";
        text_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setText", "(Ljava/lang/CharSequence;)V", false, false,
             "android.textview.set_text"},
            {"getText", "()Ljava/lang/CharSequence;", false, false,
             "android.textview.get_text"},
            {"setTextColor", "(I)V", false, false, "android.widget.noop"},
            {"setTextSize", "(F)V", false, false, "android.widget.noop"},
            {"setTextSize", "(IF)V", false, false, "android.widget.noop"},
            {"setLines", "(I)V", false, false, "android.widget.noop"},
            {"setMaxLines", "(I)V", false, false, "android.widget.noop"},
            {"setMaxWidth", "(I)V", false, false, "android.widget.noop"},
            {"setGravity", "(I)V", false, false, "android.widget.noop"},
            {"setId", "(I)V", false, false, "android.widget.noop"},
            {"setTypeface", "(Landroid/graphics/Typeface;)V", false, false,
             "android.widget.noop"},
            {"getPaint", "()Landroid/text/TextPaint;", false, false,
             "android.textview.get_paint"},
            {"addTextChangedListener", "(Landroid/text/TextWatcher;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(text_view));
        Decl button;
        button.descriptor = "Landroid/widget/Button;";
        button.superclass = "Landroid/widget/TextView;";
        button.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(button));
        Decl edit_text;
        edit_text.descriptor = "Landroid/widget/EditText;";
        edit_text.superclass = "Landroid/widget/TextView;";
        edit_text.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"getText", "()Landroid/text/Editable;", false, false,
             "android.edittext.get_editable"},
        };
        catalog.push_back(std::move(edit_text));
        Decl editable_interface;
        editable_interface.descriptor = "Landroid/text/Editable;";
        editable_interface.is_interface = true;
        editable_interface.methods = {
            {"clear", "()V", false, false, "android.editable.clear"},
            {"length", "()I", false, false, "android.editable.length"},
            {"replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
             false, false, "android.editable.replace"},
        };
        Decl editable_impl;
        editable_impl.descriptor = "Landroid/text/EditableImpl;";
        editable_impl.superclass = "Ljava/lang/Object;";
        editable_impl.interfaces = {"Landroid/text/Editable;"};
        editable_impl.methods = editable_interface.methods;
        catalog.push_back(std::move(editable_interface));
        catalog.push_back(std::move(editable_impl));
        Decl image_view;
        image_view.descriptor = "Landroid/widget/ImageView;";
        image_view.superclass = "Landroid/view/View;";
        image_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setImageResource", "(I)V", false, false,
             "android.widget.noop"},
            {"setScaleType", "(Landroid/widget/ImageView$ScaleType;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(image_view));
        Decl image_button;
        image_button.descriptor = "Landroid/widget/ImageButton;";
        image_button.superclass = "Landroid/widget/ImageView;";
        image_button.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(image_button));
        Decl scale_type;
        scale_type.descriptor = "Landroid/widget/ImageView$ScaleType;";
        scale_type.superclass = "Ljava/lang/Object;";
        scale_type.fields = {
            {"CENTER", "Landroid/widget/ImageView$ScaleType;", true, false,
             0, ""},
        };
        scale_type.clinit_handler = "android.scale_type.clinit";
        catalog.push_back(std::move(scale_type));
        Decl progress_bar;
        progress_bar.descriptor = "Landroid/widget/ProgressBar;";
        progress_bar.superclass = "Landroid/view/View;";
        progress_bar.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(progress_bar));
        // Real decoded playback through the injected VideoPlayer factory
        // (ADR-0021); without a decoder the handlers fall back to the
        // recorded-gap immediate completion.
        Decl video_view;
        video_view.descriptor = "Landroid/widget/VideoView;";
        video_view.superclass = "Landroid/view/View;";
        video_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setVideoPath", "(Ljava/lang/String;)V", false, false,
             "android.videoview.set_path"},
            {"start", "()V", false, false, "android.videoview.start"},
            {"pause", "()V", false, false, "android.videoview.pause"},
            {"seekTo", "(I)V", false, false, "android.videoview.seek_to"},
            {"stopPlayback", "()V", false, false,
             "android.videoview.stop_playback"},
            {"getDuration", "()I", false, false,
             "android.videoview.get_duration"},
            {"getCurrentPosition", "()I", false, false,
             "android.videoview.get_current_position"},
            {"setOnCompletionListener",
             "(Landroid/media/MediaPlayer$OnCompletionListener;)V", false,
             false, "android.videoview.set_completion"},
        };
        catalog.push_back(std::move(video_view));
        Decl text_paint;
        text_paint.descriptor = "Landroid/text/TextPaint;";
        text_paint.superclass = "Landroid/graphics/Paint;";
        text_paint.methods = {
            {"getTextBounds",
             "(Ljava/lang/String;IILandroid/graphics/Rect;)V", false, false,
             "android.paint.get_text_bounds"},
        };
        catalog.push_back(std::move(text_paint));
        Decl dialog_builder;
        dialog_builder.descriptor = "Landroid/app/AlertDialog$Builder;";
        dialog_builder.superclass = "Ljava/lang/Object;";
        dialog_builder.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.widget.noop"},
            {"setTitle",
             "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
             false, false, "android.widget.self"},
            {"setItems",
             "([Ljava/lang/CharSequence;"
             "Landroid/content/DialogInterface$OnClickListener;)"
             "Landroid/app/AlertDialog$Builder;",
             false, false, "android.widget.self"},
            {"create", "()Landroid/app/AlertDialog;", false, false,
             "android.dialog.create"},
        };
        catalog.push_back(std::move(dialog_builder));
        Decl alert_dialog;
        alert_dialog.descriptor = "Landroid/app/AlertDialog;";
        alert_dialog.superclass = "Ljava/lang/Object;";
        alert_dialog.methods = {
            {"show", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(alert_dialog));
        Decl progress_dialog;
        progress_dialog.descriptor = "Landroid/app/ProgressDialog;";
        progress_dialog.superclass = "Ljava/lang/Object;";
        progress_dialog.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.widget.noop"},
            {"setMessage", "(Ljava/lang/CharSequence;)V", false, false,
             "android.widget.noop"},
            {"setProgressStyle", "(I)V", false, false,
             "android.widget.noop"},
            {"show", "()V", false, false, "android.widget.noop"},
            {"dismiss", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(progress_dialog));
        // Web content is a non-goal: the widget exists, navigation is a
        // loud accounted no-op and settings answer neutral values.
        Decl web_settings;
        web_settings.descriptor = "Landroid/webkit/WebSettings;";
        web_settings.superclass = "Ljava/lang/Object;";
        web_settings.methods = {
            {"setJavaScriptEnabled", "(Z)V", false, false,
             "android.widget.noop"},
            {"getUserAgentString", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
        };
        catalog.push_back(std::move(web_settings));
        Decl web_view;
        web_view.descriptor = "Landroid/webkit/WebView;";
        web_view.superclass = "Landroid/view/ViewGroup;";
        web_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"loadUrl", "(Ljava/lang/String;)V", false, false,
             "android.webview.load_url"},
            {"getSettings", "()Landroid/webkit/WebSettings;", false, false,
             "android.webview.get_settings"},
            {"setWebViewClient", "(Landroid/webkit/WebViewClient;)V",
             false, false, "android.widget.noop"},
            {"addJavascriptInterface",
             "(Ljava/lang/Object;Ljava/lang/String;)V", false, false,
             "android.widget.noop"},
            {"clearHistory", "()V", false, false, "android.widget.noop"},
            {"goBack", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(web_view));
        Decl settings_system;
        settings_system.descriptor = "Landroid/provider/Settings$System;";
        settings_system.superclass = "Ljava/lang/Object;";
        settings_system.methods = {
            {"getInt",
             "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
             true, false, "android.settings.get_int"},
            {"putInt",
             "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z",
             true, false, "android.settings.put_int"},
        };
        catalog.push_back(std::move(settings_system));
        Decl ime;
        ime.descriptor = "Landroid/view/inputmethod/InputMethodManager;";
        ime.superclass = "Ljava/lang/Object;";
        ime.methods = {
            // No soft keyboard exists, so nothing was hidden.
            {"hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", false,
             false, "android.telephony.false"},
        };
        catalog.push_back(std::move(ime));
        Decl network_state;
        network_state.descriptor = "Landroid/net/NetworkInfo$State;";
        network_state.superclass = "Ljava/lang/Object;";
        network_state.fields = {
            {"CONNECTED", "Landroid/net/NetworkInfo$State;", true, false, 0,
             ""},
        };
        network_state.clinit_handler = "android.network_state.clinit";
        catalog.push_back(std::move(network_state));
        Decl drawable;
        drawable.descriptor = "Landroid/graphics/drawable/Drawable;";
        drawable.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(drawable));
        Decl binder;
        binder.descriptor = "Landroid/os/IBinder;";
        binder.is_interface = true;
        catalog.push_back(std::move(binder));
    }
    // Hierarchy placeholders (03 §6 layering): platform classes referenced
    // as superclass/interface by title glue code. Declaring the type is all
    // linking needs; every undeclared method hit stays an accounted,
    // explicit UnsatisfiedLinkError through the existing intrinsic route.
    // Entries are machine-evidenced by the linker's full-gap report, never
    // guessed. Real semantics (Handler dispatch etc.) arrive only per hit
    // batch; javax.net.ssl / webkit are non-goals and stay placeholders.
    struct HierarchyPlaceholder final {
        const char* descriptor;
        bool is_interface;
        const char* superclass;    // nullptr => Object for classes
        const char* interface_of;  // optional single implemented interface
        bool with_init;            // subclasses call super.<init>()
    };
    static constexpr HierarchyPlaceholder kHierarchyPlaceholders[] = {
        // Dungeon Hunter (P1000) link gap, linker report 2026-08-12.
        {"Landroid/content/DialogInterface$OnClickListener;", true, nullptr,
         nullptr, false},
        {"Landroid/text/TextWatcher;", true, nullptr, nullptr, false},
        {"Landroid/view/View$OnClickListener;", true, nullptr, nullptr,
         false},
        {"Landroid/view/View$OnTouchListener;", true, nullptr, nullptr,
         false},
        {"Landroid/webkit/WebViewClient;", false, nullptr, nullptr, true},
        {"Ljava/util/TimerTask;", false, nullptr, "Ljava/lang/Runnable;",
         true},
        {"Ljavax/net/ssl/HostnameVerifier;", true, nullptr, nullptr, false},
        {"Ljavax/net/ssl/X509TrustManager;", true, nullptr, nullptr, false},
        {"Lorg/xml/sax/helpers/DefaultHandler;", false, nullptr, nullptr,
         true},
    };
    for (const auto& placeholder : kHierarchyPlaceholders) {
        Decl declaration;
        declaration.descriptor = placeholder.descriptor;
        declaration.is_interface = placeholder.is_interface;
        if (!placeholder.is_interface) {
            declaration.superclass = placeholder.superclass != nullptr
                                         ? placeholder.superclass
                                         : "Ljava/lang/Object;";
        }
        if (placeholder.interface_of != nullptr) {
            declaration.interfaces = {placeholder.interface_of};
        }
        if (placeholder.with_init) {
            declaration.methods = {
                {"<init>", "()V", false, false, "core.object.init"},
            };
        }
        catalog.push_back(std::move(declaration));
    }
}

}  // namespace ogplay::runtime::android_intrinsics
