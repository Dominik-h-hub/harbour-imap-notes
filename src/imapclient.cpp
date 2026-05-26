#include "imapclient.h"

#include <libetpan/libetpan.h>

#include <QtDebug>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

namespace {

// libetpan owns the raw buffer returned by mailimap_fetch — we copy what we
// need before freeing the result list.
QByteArray copyMessagePayload(clistiter *att_iter)
{
    QByteArray out;
    for (; att_iter; att_iter = clist_next(att_iter)) {
        auto *att_item = static_cast<mailimap_msg_att_item *>(clist_content(att_iter));
        if (att_item->att_type != MAILIMAP_MSG_ATT_ITEM_STATIC) continue;
        auto *att_static = att_item->att_data.att_static;
        if (att_static->att_type == MAILIMAP_MSG_ATT_BODY_SECTION) {
            const char *data = att_static->att_data.att_body_section->sec_body_part;
            const size_t length = att_static->att_data.att_body_section->sec_length;
            if (data && length > 0) {
                out = QByteArray(data, static_cast<int>(length));
            }
        } else if (att_static->att_type == MAILIMAP_MSG_ATT_RFC822) {
            const char *data = att_static->att_data.att_rfc822.att_content;
            const size_t length = att_static->att_data.att_rfc822.att_length;
            if (data && length > 0) {
                out = QByteArray(data, static_cast<int>(length));
            }
        }
    }
    return out;
}

quint32 readUid(clistiter *att_iter)
{
    for (; att_iter; att_iter = clist_next(att_iter)) {
        auto *att_item = static_cast<mailimap_msg_att_item *>(clist_content(att_iter));
        if (att_item->att_type != MAILIMAP_MSG_ATT_ITEM_STATIC) continue;
        auto *att_static = att_item->att_data.att_static;
        if (att_static->att_type == MAILIMAP_MSG_ATT_UID) {
            return att_static->att_data.att_uid;
        }
    }
    return 0;
}

QString readHeaderValue(clistiter *att_iter, const QString &name)
{
    for (; att_iter; att_iter = clist_next(att_iter)) {
        auto *att_item = static_cast<mailimap_msg_att_item *>(clist_content(att_iter));
        if (att_item->att_type != MAILIMAP_MSG_ATT_ITEM_STATIC) continue;
        auto *att_static = att_item->att_data.att_static;
        if (att_static->att_type != MAILIMAP_MSG_ATT_BODY_SECTION) continue;
        const char *data = att_static->att_data.att_body_section->sec_body_part;
        const size_t length = att_static->att_data.att_body_section->sec_length;
        if (!data || length == 0) continue;

        const QByteArray raw(data, static_cast<int>(length));
        const QByteArray needle = name.toLatin1() + ':';
        const int idx = raw.toLower().indexOf(needle.toLower());
        if (idx < 0) continue;
        int end = raw.indexOf('\n', idx);
        if (end < 0) end = raw.size();
        QString line = QString::fromLatin1(raw.mid(idx + needle.size(), end - idx - needle.size()));
        return line.trimmed();
    }
    return {};
}

} // namespace

ImapClient::ImapClient(QObject *parent)
    : QObject(parent)
{
}

ImapClient::~ImapClient()
{
    disconnectFromHost();
    closeCancelPipe();
}

bool ImapClient::ensureCancelPipe()
{
    if (m_cancelPipe[0] != -1) return true;
    if (::pipe(m_cancelPipe) != 0) {
        m_cancelPipe[0] = -1;
        m_cancelPipe[1] = -1;
        return false;
    }
    // Reads should never block — the caller drains the pipe inside select().
    ::fcntl(m_cancelPipe[0], F_SETFL, O_NONBLOCK);
    ::fcntl(m_cancelPipe[1], F_SETFL, O_NONBLOCK);
    return true;
}

void ImapClient::closeCancelPipe()
{
    if (m_cancelPipe[0] != -1) ::close(m_cancelPipe[0]);
    if (m_cancelPipe[1] != -1) ::close(m_cancelPipe[1]);
    m_cancelPipe[0] = m_cancelPipe[1] = -1;
}

bool ImapClient::isConnected() const
{
    return m_session != nullptr;
}

bool ImapClient::hasCapability(const QString &name) const
{
    return m_capabilities.contains(name.toUpper());
}

void ImapClient::recordEtpanError(int code, const QString &context)
{
    QString message = context;
    if (m_session && m_session->imap_response) {
        message += QStringLiteral(": ") + QString::fromUtf8(m_session->imap_response);
    } else {
        message += QStringLiteral(": libetpan error %1").arg(code);
    }
    m_lastError = message;
    emit error(m_lastError);
    qWarning() << "ImapClient:" << m_lastError;
}

bool ImapClient::connectToHost(const QString &host, int port, Security security)
{
    if (m_session) {
        disconnectFromHost();
    }
    m_session = mailimap_new(0, nullptr);
    if (!m_session) {
        m_lastError = QStringLiteral("mailimap_new failed");
        return false;
    }

    int r = (security == ImplicitSsl)
        ? mailimap_ssl_connect(m_session, host.toUtf8().constData(), static_cast<quint16>(port))
        : mailimap_socket_connect(m_session, host.toUtf8().constData(), static_cast<quint16>(port));
    if (r != MAILIMAP_NO_ERROR && r != MAILIMAP_NO_ERROR_AUTHENTICATED && r != MAILIMAP_NO_ERROR_NON_AUTHENTICATED) {
        recordEtpanError(r, QStringLiteral("connect to %1:%2").arg(host).arg(port));
        mailimap_free(m_session);
        m_session = nullptr;
        return false;
    }

    if (security == StartTls) {
        r = mailimap_socket_starttls(m_session);
        if (r != MAILIMAP_NO_ERROR) {
            recordEtpanError(r, QStringLiteral("STARTTLS"));
            mailimap_free(m_session);
            m_session = nullptr;
            return false;
        }
    }

    return true;
}

bool ImapClient::login(const QString &username, const QString &password)
{
    if (!m_session) {
        m_lastError = QStringLiteral("Not connected");
        return false;
    }
    const int r = mailimap_login(m_session,
                                 username.toUtf8().constData(),
                                 password.toUtf8().constData());
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("LOGIN"));
        return false;
    }
    return refreshCapabilities();
}

bool ImapClient::refreshCapabilities()
{
    m_capabilities.clear();
    mailimap_capability_data *caps = nullptr;
    const int r = mailimap_capability(m_session, &caps);
    if (r != MAILIMAP_NO_ERROR || !caps) {
        return true; // server may not advertise — not fatal
    }
    for (clistiter *it = clist_begin(caps->cap_list); it; it = clist_next(it)) {
        auto *cap = static_cast<mailimap_capability *>(clist_content(it));
        if (cap->cap_type == MAILIMAP_CAPABILITY_NAME && cap->cap_data.cap_name) {
            m_capabilities.insert(QString::fromLatin1(cap->cap_data.cap_name).toUpper());
        }
    }
    mailimap_capability_data_free(caps);
    return true;
}

void ImapClient::disconnectFromHost()
{
    if (!m_session) return;
    // logout can fail if the connection is already half-broken — ignore errors
    // here, we are tearing down anyway.
    mailimap_logout(m_session);
    mailimap_free(m_session);
    m_session = nullptr;
    m_selectedFolder.clear();
    m_capabilities.clear();
    emit disconnected();
}

bool ImapClient::listFolders(const QString &reference,
                             const QString &pattern,
                             QList<FolderInfo> *out)
{
    if (!m_session || !out) return false;

    clist *list_result = nullptr;
    const int r = mailimap_list(m_session,
                                reference.toUtf8().constData(),
                                pattern.toUtf8().constData(),
                                &list_result);
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("LIST %1 %2").arg(reference, pattern));
        return false;
    }

    for (clistiter *it = clist_begin(list_result); it; it = clist_next(it)) {
        auto *mbx = static_cast<mailimap_mailbox_list *>(clist_content(it));
        FolderInfo info;
        info.fullPath = QString::fromUtf8(mbx->mb_name);
        if (mbx->mb_delimiter) {
            info.delimiter = QString(QChar(mbx->mb_delimiter));
            m_hierarchyDelimiter = info.delimiter;
        } else {
            info.delimiter = m_hierarchyDelimiter;
        }
        if (mbx->mb_flag && mbx->mb_flag->mbf_sflag == MAILIMAP_MBX_LIST_SFLAG_NOSELECT) {
            info.selectable = false;
        }
        out->append(info);
    }
    mailimap_list_result_free(list_result);
    return true;
}

bool ImapClient::createFolder(const QString &path)
{
    if (!m_session) return false;
    const int r = mailimap_create(m_session, path.toUtf8().constData());
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("CREATE %1").arg(path));
        return false;
    }
    return true;
}

bool ImapClient::deleteFolder(const QString &path)
{
    if (!m_session) return false;
    const int r = mailimap_delete(m_session, path.toUtf8().constData());
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("DELETE %1").arg(path));
        return false;
    }
    return true;
}

bool ImapClient::renameFolder(const QString &oldPath, const QString &newPath)
{
    if (!m_session) return false;
    const int r = mailimap_rename(m_session,
                                  oldPath.toUtf8().constData(),
                                  newPath.toUtf8().constData());
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("RENAME %1 -> %2").arg(oldPath, newPath));
        return false;
    }
    return true;
}

bool ImapClient::selectFolder(const QString &path, SelectResult *out)
{
    if (!m_session) return false;
    const int r = mailimap_select(m_session, path.toUtf8().constData());
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("SELECT %1").arg(path));
        return false;
    }
    m_selectedFolder = path;
    if (out) {
        out->uidValidity = m_session->imap_selection_info
            ? m_session->imap_selection_info->sel_uidvalidity : 0;
        out->uidNext = m_session->imap_selection_info
            ? m_session->imap_selection_info->sel_uidnext : 0;
        out->exists = m_session->imap_selection_info
            ? m_session->imap_selection_info->sel_exists : 0;
        // libetpan keeps the modseq on the optional condstore part of the
        // selection info; if CONDSTORE isn't supported it stays 0.
        out->highestModSeq = 0;
    }
    return true;
}

bool ImapClient::fetchAllUids(QList<quint32> *out)
{
    return fetchUidsSince(1, out);
}

bool ImapClient::fetchUidsSince(quint32 sinceUid, QList<quint32> *out)
{
    if (!m_session || !out) return false;

    mailimap_set *set = mailimap_set_new_interval(sinceUid, 0);
    mailimap_fetch_type *fetch_type = mailimap_fetch_type_new_fetch_att_list_empty();
    mailimap_fetch_type_new_fetch_att_list_add(fetch_type, mailimap_fetch_att_new_uid());

    clist *fetch_result = nullptr;
    const int r = mailimap_uid_fetch(m_session, set, fetch_type, &fetch_result);
    mailimap_set_free(set);
    mailimap_fetch_type_free(fetch_type);
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("UID FETCH %1:* (UID)").arg(sinceUid));
        return false;
    }

    for (clistiter *it = clist_begin(fetch_result); it; it = clist_next(it)) {
        auto *msg = static_cast<mailimap_msg_att *>(clist_content(it));
        const quint32 uid = readUid(clist_begin(msg->att_list));
        if (uid > 0) out->append(uid);
    }
    mailimap_fetch_list_free(fetch_result);
    return true;
}

bool ImapClient::fetchFullMessage(quint32 uid, QByteArray *out)
{
    if (!m_session || !out) return false;

    mailimap_set *set = mailimap_set_new_single(uid);
    mailimap_fetch_type *fetch_type = mailimap_fetch_type_new_fetch_att_list_empty();
    mailimap_section *section = mailimap_section_new(nullptr); // whole body
    mailimap_fetch_type_new_fetch_att_list_add(
        fetch_type, mailimap_fetch_att_new_body_peek_section(section));

    clist *fetch_result = nullptr;
    const int r = mailimap_uid_fetch(m_session, set, fetch_type, &fetch_result);
    mailimap_set_free(set);
    mailimap_fetch_type_free(fetch_type);
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("UID FETCH %1 BODY.PEEK[]").arg(uid));
        return false;
    }

    QByteArray payload;
    for (clistiter *it = clist_begin(fetch_result); it; it = clist_next(it)) {
        auto *msg = static_cast<mailimap_msg_att *>(clist_content(it));
        payload = copyMessagePayload(clist_begin(msg->att_list));
        if (!payload.isEmpty()) break;
    }
    mailimap_fetch_list_free(fetch_result);

    if (payload.isEmpty()) {
        m_lastError = QStringLiteral("Empty body for UID %1").arg(uid);
        return false;
    }
    *out = payload;
    return true;
}

bool ImapClient::fetchHeader(quint32 uid, const QString &headerName, QString *out)
{
    if (!m_session || !out) return false;

    mailimap_set *set = mailimap_set_new_single(uid);
    mailimap_fetch_type *fetch_type = mailimap_fetch_type_new_fetch_att_list_empty();

    clist *header_list = clist_new();
    char *hdrcopy = strdup(headerName.toUtf8().constData());
    clist_append(header_list, hdrcopy);
    mailimap_header_list *hdrs = mailimap_header_list_new(header_list);
    mailimap_section *section = mailimap_section_new_header_fields(hdrs);
    mailimap_fetch_type_new_fetch_att_list_add(
        fetch_type, mailimap_fetch_att_new_body_peek_section(section));

    clist *fetch_result = nullptr;
    const int r = mailimap_uid_fetch(m_session, set, fetch_type, &fetch_result);
    mailimap_set_free(set);
    mailimap_fetch_type_free(fetch_type);
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("UID FETCH %1 HEADER.FIELDS").arg(uid));
        return false;
    }

    QString value;
    for (clistiter *it = clist_begin(fetch_result); it; it = clist_next(it)) {
        auto *msg = static_cast<mailimap_msg_att *>(clist_content(it));
        value = readHeaderValue(clist_begin(msg->att_list), headerName);
        if (!value.isEmpty()) break;
    }
    mailimap_fetch_list_free(fetch_result);
    *out = value;
    return true;
}

bool ImapClient::appendMessage(const QString &folder,
                               const QByteArray &rawMessage,
                               quint32 *newUid)
{
    if (!m_session) return false;

    if (newUid) *newUid = 0;

    if (hasCapability(QStringLiteral("UIDPLUS"))) {
        uint32_t uidvalidity = 0;
        uint32_t out_uid = 0;
        const int r = mailimap_uidplus_append(m_session,
                                              folder.toUtf8().constData(),
                                              nullptr,
                                              nullptr,
                                              rawMessage.constData(),
                                              static_cast<size_t>(rawMessage.size()),
                                              &uidvalidity,
                                              &out_uid);
        if (r != MAILIMAP_NO_ERROR) {
            recordEtpanError(r, QStringLiteral("APPEND %1").arg(folder));
            return false;
        }
        if (newUid) *newUid = out_uid;
        return true;
    }

    const int r = mailimap_append(m_session,
                                  folder.toUtf8().constData(),
                                  nullptr,
                                  nullptr,
                                  rawMessage.constData(),
                                  static_cast<size_t>(rawMessage.size()));
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("APPEND %1").arg(folder));
        return false;
    }
    return true;
}

bool ImapClient::markDeletedAndExpunge(const QList<quint32> &uids)
{
    if (!m_session || uids.isEmpty()) return uids.isEmpty();

    mailimap_set *set = mailimap_set_new_empty();
    for (quint32 uid : uids) {
        mailimap_set_add_single(set, uid);
    }

    mailimap_flag_list *flag_list = mailimap_flag_list_new_empty();
    mailimap_flag_list_add(flag_list, mailimap_flag_new_deleted());
    mailimap_store_att_flags *store_atts = mailimap_store_att_flags_new_add_flags_silent(flag_list);

    const int r = mailimap_uid_store(m_session, set, store_atts);
    mailimap_set_free(set);
    mailimap_store_att_flags_free(store_atts);

    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("UID STORE \\Deleted"));
        return false;
    }

    const int er = mailimap_expunge(m_session);
    if (er != MAILIMAP_NO_ERROR) {
        recordEtpanError(er, QStringLiteral("EXPUNGE"));
        return false;
    }
    return true;
}

bool ImapClient::moveMessage(quint32 uid, const QString &destinationFolder)
{
    if (!m_session) return false;

    mailimap_set *set = mailimap_set_new_single(uid);

    // RFC 6851 MOVE — preferred when the server advertises it because it
    // performs the copy and the source-side delete atomically.
    if (hasCapability(QStringLiteral("MOVE"))) {
        const int r = mailimap_uid_move(m_session, set, destinationFolder.toUtf8().constData());
        mailimap_set_free(set);
        if (r != MAILIMAP_NO_ERROR) {
            recordEtpanError(r, QStringLiteral("UID MOVE -> %1").arg(destinationFolder));
            return false;
        }
        return true;
    }

    const int rc = mailimap_uid_copy(m_session, set, destinationFolder.toUtf8().constData());
    if (rc != MAILIMAP_NO_ERROR) {
        mailimap_set_free(set);
        recordEtpanError(rc, QStringLiteral("UID COPY -> %1").arg(destinationFolder));
        return false;
    }

    mailimap_flag_list *flag_list = mailimap_flag_list_new_empty();
    mailimap_flag_list_add(flag_list, mailimap_flag_new_deleted());
    mailimap_store_att_flags *store_atts = mailimap_store_att_flags_new_add_flags_silent(flag_list);
    const int rs = mailimap_uid_store(m_session, set, store_atts);
    mailimap_set_free(set);
    mailimap_store_att_flags_free(store_atts);
    if (rs != MAILIMAP_NO_ERROR) {
        recordEtpanError(rs, QStringLiteral("UID STORE \\Deleted (fallback move)"));
        return false;
    }

    const int re = mailimap_expunge(m_session);
    if (re != MAILIMAP_NO_ERROR) {
        recordEtpanError(re, QStringLiteral("EXPUNGE (fallback move)"));
        return false;
    }
    return true;
}

bool ImapClient::waitForActivity(int maxSeconds)
{
    if (!m_session) return false;
    if (!hasCapability(QStringLiteral("IDLE"))) {
        // No IDLE — caller falls back to interval polling.
        m_lastError = QStringLiteral("Server does not advertise IDLE");
        return false;
    }
    if (!ensureCancelPipe()) {
        m_lastError = QStringLiteral("Cannot create cancel pipe");
        return false;
    }

    const int r = mailimap_idle(m_session);
    if (r != MAILIMAP_NO_ERROR) {
        recordEtpanError(r, QStringLiteral("IDLE"));
        return false;
    }

    const int imapFd = mailimap_idle_get_fd(m_session);
    const int cancelFd = m_cancelPipe[0];
    int nfds = (imapFd > cancelFd ? imapFd : cancelFd) + 1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(imapFd, &rfds);
    FD_SET(cancelFd, &rfds);

    timeval tv;
    tv.tv_sec = maxSeconds;
    tv.tv_usec = 0;

    const int sel = ::select(nfds, &rfds, nullptr, nullptr, &tv);

    // Drain the cancel pipe so the next IDLE starts clean.
    if (sel > 0 && FD_ISSET(cancelFd, &rfds)) {
        char drain[64];
        while (::read(cancelFd, drain, sizeof(drain)) > 0) {}
    }

    const int done = mailimap_idle_done(m_session);
    if (done != MAILIMAP_NO_ERROR) {
        recordEtpanError(done, QStringLiteral("IDLE DONE"));
        return false;
    }

    if (sel < 0) {
        m_lastError = QStringLiteral("select() failed during IDLE");
        return false;
    }
    if (sel == 0) {
        return false; // timeout
    }
    // Activity on the imap socket means the server pushed an untagged
    // response (EXISTS/EXPUNGE/FETCH). The DONE roundtrip above already
    // consumed and parsed it, so the caller should now refresh state.
    return FD_ISSET(imapFd, &rfds);
}

void ImapClient::cancelIdle()
{
    if (m_cancelPipe[1] == -1) return;
    const char b = 'x';
    ::write(m_cancelPipe[1], &b, 1);
}
