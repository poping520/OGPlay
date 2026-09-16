package org.ogplay.security;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.Key;
import java.security.KeyStoreSpi;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.Collections;
import java.util.Date;
import java.util.Enumeration;
import java.util.Hashtable;

/** API 19 read-only AndroidCAStore view over an injected CA pack. */
public final class AndroidCaStoreSpi extends KeyStoreSpi {
    private Hashtable aliases = new Hashtable();
    private boolean loaded;

    public Key engineGetKey(String alias, char[] password) {
        requireAlias(alias);
        return null;
    }

    public Certificate[] engineGetCertificateChain(String alias) {
        requireAlias(alias);
        return null;
    }

    public Certificate engineGetCertificate(String alias) {
        Entry entry = (Entry) aliases.get(requireAlias(alias));
        return entry == null ? null : entry.certificate;
    }

    public Date engineGetCreationDate(String alias) {
        Entry entry = (Entry) aliases.get(requireAlias(alias));
        return entry == null ? null : new Date(entry.created.getTime());
    }

    public void engineSetKeyEntry(String alias, Key key, char[] password, Certificate[] chain) {
        throw new UnsupportedOperationException();
    }

    public void engineSetKeyEntry(String alias, byte[] key, Certificate[] chain) {
        throw new UnsupportedOperationException();
    }

    public void engineSetCertificateEntry(String alias, Certificate cert) {
        requireAlias(alias);
        throw new UnsupportedOperationException();
    }

    public void engineDeleteEntry(String alias) {
        throw new UnsupportedOperationException();
    }

    public Enumeration engineAliases() {
        return Collections.enumeration(aliases.keySet());
    }

    public boolean engineContainsAlias(String alias) {
        return aliases.containsKey(requireAlias(alias));
    }

    public int engineSize() {
        return aliases.size();
    }

    public boolean engineIsKeyEntry(String alias) {
        requireAlias(alias);
        return false;
    }

    public boolean engineIsCertificateEntry(String alias) {
        return engineContainsAlias(alias);
    }

    public String engineGetCertificateAlias(Certificate cert) {
        if (!(cert instanceof X509Certificate)) {
            return null;
        }
        Enumeration keys = aliases.keys();
        while (keys.hasMoreElements()) {
            String alias = (String) keys.nextElement();
            Entry entry = (Entry) aliases.get(alias);
            if (entry.certificate.equals(cert)) {
                return alias;
            }
        }
        return null;
    }

    public void engineStore(OutputStream stream, char[] password) {
        throw new UnsupportedOperationException();
    }

    public void engineLoad(InputStream stream, char[] password)
            throws IOException, CertificateException {
        if (stream != null) {
            throw new UnsupportedOperationException();
        }
        CaBundle bundle = CaBundle.loadDefault();
        Hashtable next = new Hashtable();
        for (int i = 0; i < bundle.aliases.length; i++) {
            next.put(bundle.aliases[i], new Entry(bundle.certificates[i], bundle.dates[i]));
        }
        aliases = next;
        loaded = true;
    }

    private static String requireAlias(String alias) {
        if (alias == null) {
            throw new NullPointerException("alias == null");
        }
        return alias;
    }

    private static final class Entry {
        final X509Certificate certificate;
        final Date created;

        Entry(X509Certificate certificate, Date created) {
            this.certificate = certificate;
            this.created = created;
        }
    }
}
