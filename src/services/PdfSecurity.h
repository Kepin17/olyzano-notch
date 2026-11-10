#pragma once

#include <QString>
#include <QStringList>

// Locking a PDF with a password and opening one again, with Ghostscript's pdfwrite. This file makes the arguments and checks the passwords;
// it runs nothing. The passwords go to Ghostscript in an argument file (`@file`) that only the person's account can read, never on the command line.
// Ghostscript writes encryption revision 3 only: 128-bit RC4. That keeps a file from being opened casually; it is not what a determined attacker needs
// to be stopped by (the UI and docs say so). Pure.
namespace PdfSecurity {

struct LockOptions {
    QString userPassword;    // asked when the file is opened
    QString ownerPassword;   // lets someone change the permissions; made at random by the caller when the person gave none
    bool allowPrint = true;
    bool allowCopy = true;   // selecting and copying text
    bool allowChange = true; // editing and adding notes
};

constexpr int MinPassword = 4;
constexpr int MaxPassword = 32;

// "" when the password can be used: 4–32 printable ASCII characters without spaces (the argument file is split at white space, and PDF
// passwords are read as Latin-1 by other programs).
inline QString validatePassword(const QString &password)
{
    if (password.size() < MinPassword)
        return QStringLiteral("The password needs at least %1 characters.").arg(MinPassword);
    if (password.size() > MaxPassword)
        return QStringLiteral("The password can have at most %1 characters.").arg(MaxPassword);
    for (const QChar c : password)
        if (c.unicode() <= 0x20 || c.unicode() >= 0x7F)
            return QStringLiteral("Use only letters, digits and symbols, without spaces or accents.");
    return {};
}

// The permission bits of the PDF standard security handler as the signed number Ghostscript takes (-dPermissions).
// Bits 7, 8 and 13–32 are always set; print is bit 3 (+12 high quality), change is bit 4 (+9 forms, +11 pages) and bit 6 (notes), copy is bit 5 (+10 reading aloud).
inline int permissions(const LockOptions &o)
{
    quint32 p = 0xFFFFF0C0u;
    if (o.allowPrint) p |= (1u << 2) | (1u << 11);
    if (o.allowChange) p |= (1u << 3) | (1u << 5) | (1u << 8) | (1u << 10);
    if (o.allowCopy) p |= (1u << 4) | (1u << 9);
    return int(p);
}

// The text of the argument file for locking. One argument per line.
inline QString lockArgs(const LockOptions &o)
{
    return QStringLiteral("-sOwnerPassword=%1\n-sUserPassword=%2\n-dEncryptionR=3\n-dKeyLength=128\n-dPermissions=%3\n").arg(o.ownerPassword, o.userPassword).arg(permissions(o));
}

inline QString unlockArgs(const QString &password) { return QStringLiteral("-sPDFPassword=%1\n").arg(password); }

// Ghostscript's other arguments (the passwords are in the argument file, named first).
inline QStringList gsArgs(const QString &argFile, const QString &in, const QString &out)
{
    return {QStringLiteral("@") + argFile, QStringLiteral("-sDEVICE=pdfwrite"), QStringLiteral("-dCompatibilityLevel=1.6"), QStringLiteral("-dPassThroughJPEGImages=true"),
            QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"), QStringLiteral("-dQUIET"), QStringLiteral("-sOutputFile=") + out, in};
}

// Did Ghostscript or poppler say the password was missing or wrong?
inline bool isPasswordProblem(const QString &stderrText)
{
    const QString t = stderrText.toLower();
    return t.contains(QLatin1String("password")) || t.contains(QLatin1String("cannot decrypt")) || t.contains(QLatin1String("encrypted"));
}

} // namespace PdfSecurity
