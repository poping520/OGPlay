package io.github.poping520.ogplay.launcher;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Directories picked through SAF, kept as handles ("saf-tree:N") instead of copies: the native import lists a
 * handle's files and opens them one by one while it writes them into the library. A listing is read once per
 * handle and kept, so the preview and the import see the same files.
 */
final class SafTrees {
    private static final String TAG = "OGPlay";
    static final String PREFIX = "saf-tree:";

    private static final class Tree {
        final Uri uri;
        List<String[]> files;   // {path, size, document id}
        Map<String, String> documents;

        Tree(Uri uri) {
            this.uri = uri;
        }
    }

    private final ContentResolver resolver;
    private final Map<String, Tree> trees = new HashMap<>();
    private int next;

    SafTrees(ContentResolver resolver) {
        this.resolver = resolver;
    }

    synchronized String register(Uri tree) {
        String handle = PREFIX + (++next);
        trees.put(handle, new Tree(tree));
        return handle;
    }

    /** Where the directory sits on its volume, as the host's mount guess reads it: /sdcard/… or /storage/<id>/…. */
    static String label(Uri tree) {
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        String volume = colon < 0 ? id : id.substring(0, colon), path = colon < 0 ? "" : id.substring(colon + 1);
        String root = "primary".equalsIgnoreCase(volume) ? "/sdcard" : "/storage/" + volume;
        return path.isEmpty() ? root : root + "/" + path;
    }

    /** "size\tpath\n" for every file below the handle (UTF-8); null for an unknown handle or an unreadable tree. */
    byte[] list(String handle) {
        Tree tree = find(handle);
        if (tree == null) return null;
        synchronized (tree) {
            if (tree.files == null) {
                List<String[]> files = new ArrayList<>();
                try {
                    walk(tree.uri, DocumentsContract.getTreeDocumentId(tree.uri), "", files);
                } catch (IOException | RuntimeException error) {
                    Log.e(TAG, "cannot list the picked directory", error);
                    return null;
                }
                Map<String, String> documents = new HashMap<>();
                for (String[] file : files) documents.put(file[0], file[2]);
                tree.files = files;
                tree.documents = documents;
            }
            StringBuilder text = new StringBuilder();
            for (String[] file : tree.files) text.append(file[1]).append('\t').append(file[0]).append('\n');
            return text.toString().getBytes(StandardCharsets.UTF_8);
        }
    }

    /** A detached read descriptor for one listed file, owned by the caller; -1 when it cannot be opened. */
    int open(String handle, String path) {
        Tree tree = find(handle);
        if (tree == null) return -1;
        String document;
        synchronized (tree) {
            document = tree.documents == null ? null : tree.documents.get(path);
        }
        if (document == null) return -1;
        try {
            ParcelFileDescriptor descriptor = resolver.openFileDescriptor(
                    DocumentsContract.buildDocumentUriUsingTree(tree.uri, document), "r");
            return descriptor == null ? -1 : descriptor.detachFd();
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "cannot open " + path, error);
            return -1;
        }
    }

    private synchronized Tree find(String handle) {
        return trees.get(handle);
    }

    private void walk(Uri tree, String id, String prefix, List<String[]> files) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE};
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("cannot list " + id);
            while (cursor.moveToNext()) {
                String child = cursor.getString(0), name = cursor.getString(1), type = cursor.getString(2);
                if (!safeName(name)) continue;
                String path = prefix.isEmpty() ? name : prefix + "/" + name;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(type)) {
                    walk(tree, child, path, files);
                } else {
                    files.add(new String[] {path, String.valueOf(cursor.isNull(3) ? 0 : cursor.getLong(3)), child});
                }
            }
        }
    }

    private static boolean safeName(String name) {
        return name != null && !name.isEmpty() && !name.equals(".") && !name.equals("..")
                && name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf('\0') < 0
                && name.indexOf('\t') < 0 && name.indexOf('\n') < 0;
    }
}
