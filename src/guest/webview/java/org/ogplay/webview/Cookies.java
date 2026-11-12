package org.ogplay.webview;

import android.net.Uri;
import android.net.WebAddress;
import android.webkit.CookieManager;

import java.net.HttpCookie;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;

final class Cookies extends CookieManager {
    private static final class Entry {
        final HttpCookie cookie;
        final String domain;
        final String path;
        final boolean hostOnly;

        Entry(HttpCookie cookie, String domain, String path, boolean hostOnly) {
            this.cookie = cookie;
            this.domain = domain;
            this.path = path;
            this.hostOnly = hostOnly;
        }
    }

    private final List<Entry> entries = new ArrayList<Entry>();
    private boolean accept = true;
    private boolean allowFiles;
    private boolean used;

    @Override public synchronized void setAcceptCookie(boolean value) { accept = value; }
    @Override public synchronized boolean acceptCookie() { return accept; }

    @Override public synchronized void setCookie(String url, String value) {
        used = true;
        if (url == null || value == null) return;
        Uri uri = parse(url);
        String host = host(uri);
        if (host == null) return;
        List<HttpCookie> parsed;
        try {
            parsed = HttpCookie.parse(value);
        } catch (IllegalArgumentException e) {
            return;
        }
        for (HttpCookie cookie : parsed) {
            boolean hostOnly = cookie.getDomain() == null;
            String domain = hostOnly ? host : cookie.getDomain().toLowerCase(Locale.US);
            if (domain.startsWith(".")) domain = domain.substring(1);
            if (!domainMatches(host, domain, hostOnly)) continue;
            if (cookie.getSecure() && !"https".equalsIgnoreCase(uri.getScheme())) continue;
            String path = cookie.getPath();
            if (path == null || !path.startsWith("/")) {
                String requestPath = path(uri);
                int slash = requestPath.lastIndexOf('/');
                path = slash <= 0 ? "/" : requestPath.substring(0, slash);
            }
            for (Iterator<Entry> it = entries.iterator(); it.hasNext();) {
                Entry entry = it.next();
                if (entry.domain.equals(domain) && entry.path.equals(path)
                        && entry.cookie.getName().equals(cookie.getName())) {
                    it.remove();
                    break;
                }
            }
            if (!cookie.hasExpired()) entries.add(new Entry(cookie, domain, path, hostOnly));
        }
    }

    @Override public synchronized String getCookie(String url) { return getCookie(url, false); }
    @Override public synchronized String getCookie(WebAddress url) {
        return url == null ? null : getCookie(url.toString());
    }

    @Override public synchronized String getCookie(String url, boolean privateBrowsing) {
        used = true;
        if (privateBrowsing || url == null) return null;
        Uri uri = parse(url);
        String host = host(uri);
        if (host == null) return null;
        String path = path(uri);
        boolean secure = "https".equalsIgnoreCase(uri.getScheme());
        removeExpiredCookie();
        List<Entry> matches = new ArrayList<Entry>();
        for (Entry entry : entries) {
            if (domainMatches(host, entry.domain, entry.hostOnly) && pathMatches(path, entry.path)
                    && (!entry.cookie.getSecure() || secure)) {
                int index = 0;
                while (index < matches.size() && matches.get(index).path.length() >= entry.path.length()) {
                    index++;
                }
                matches.add(index, entry);
            }
        }
        StringBuilder result = new StringBuilder();
        for (Entry entry : matches) {
            if (result.length() != 0) result.append("; ");
            result.append(entry.cookie.getName()).append('=').append(entry.cookie.getValue());
        }
        return result.length() == 0 ? null : result.toString();
    }

    @Override public synchronized boolean hasCookies() { return hasCookies(false); }
    @Override public synchronized boolean hasCookies(boolean privateBrowsing) {
        used = true;
        removeExpiredCookie();
        return !privateBrowsing && !entries.isEmpty();
    }

    @Override public synchronized void removeSessionCookie() {
        for (Iterator<Entry> it = entries.iterator(); it.hasNext();) {
            if (it.next().cookie.getMaxAge() == -1) it.remove();
        }
    }
    @Override public synchronized void removeAllCookie() { entries.clear(); }
    @Override public synchronized void removeExpiredCookie() {
        for (Iterator<Entry> it = entries.iterator(); it.hasNext();) {
            if (it.next().cookie.hasExpired()) it.remove();
        }
    }

    @Override protected void flushCookieStore() {}
    @Override protected synchronized boolean allowFileSchemeCookiesImpl() { return allowFiles; }
    @Override protected synchronized void setAcceptFileSchemeCookiesImpl(boolean value) {
        if (!used) allowFiles = value;
    }

    private static Uri parse(String url) {
        Uri uri = Uri.parse(url);
        // CookieManager also accepts host names without an explicit scheme.
        return uri.getScheme() == null || !uri.isHierarchical() ? Uri.parse("http://" + url) : uri;
    }

    private String host(Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) return allowFiles ? "file://" : null;
        if (!"http".equalsIgnoreCase(uri.getScheme()) && !"https".equalsIgnoreCase(uri.getScheme())) return null;
        String host = uri.getHost();
        return host == null || host.isEmpty() ? null : host.toLowerCase(Locale.US);
    }

    private static String path(Uri uri) {
        String path = uri.getEncodedPath();
        return path == null || path.isEmpty() ? "/" : path;
    }

    private static boolean domainMatches(String host, String domain, boolean hostOnly) {
        return host.equals(domain) || (!hostOnly && !domain.isEmpty() && host.endsWith("." + domain));
    }

    private static boolean pathMatches(String request, String cookie) {
        return request.equals(cookie) || (request.startsWith(cookie)
                && (cookie.endsWith("/") || request.charAt(cookie.length()) == '/'));
    }
}
