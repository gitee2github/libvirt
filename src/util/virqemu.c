/*
 * virqemu.c: utilities for working with qemu and its tools
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */


#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "virbuffer.h"
#include "virerror.h"
#include "virlog.h"
#include "virqemu.h"
#include "virstring.h"
#include "viralloc.h"
#include "virfile.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.qemu");

struct virQEMUCommandLineJSONIteratorData {
    const char *prefix;
    virBufferPtr buf;
    const char *skipKey;
    bool onOff;
    virQEMUBuildCommandLineJSONArrayFormatFunc arrayFunc;
};


static int
virQEMUBuildCommandLineJSONRecurse(const char *key,
                                   virJSONValuePtr value,
                                   virBufferPtr buf,
                                   const char *skipKey,
                                   bool onOff,
                                   virQEMUBuildCommandLineJSONArrayFormatFunc arrayFunc,
                                   bool nested);



int
virQEMUBuildCommandLineJSONArrayBitmap(const char *key,
                                       virJSONValuePtr array,
                                       virBufferPtr buf,
                                       const char *skipKey G_GNUC_UNUSED,
                                       bool onOff G_GNUC_UNUSED)
{
    ssize_t pos = -1;
    ssize_t end;
    g_autoptr(virBitmap) bitmap = NULL;

    if (virJSONValueGetArrayAsBitmap(array, &bitmap) < 0)
        return -1;

    while ((pos = virBitmapNextSetBit(bitmap, pos)) > -1) {
        if ((end = virBitmapNextClearBit(bitmap, pos)) < 0)
            end = virBitmapLastSetBit(bitmap) + 1;

        if (end - 1 > pos) {
            virBufferAsprintf(buf, "%s=%zd-%zd,", key, pos, end - 1);
            pos = end;
        } else {
            virBufferAsprintf(buf, "%s=%zd,", key, pos);
        }
    }

    return 0;
}


int
virQEMUBuildCommandLineJSONArrayNumbered(const char *key,
                                         virJSONValuePtr array,
                                         virBufferPtr buf,
                                         const char *skipKey,
                                         bool onOff)
{
    virJSONValuePtr member;
    size_t i;

    for (i = 0; i < virJSONValueArraySize(array); i++) {
        member = virJSONValueArrayGet((virJSONValuePtr) array, i);
        g_autofree char *prefix = NULL;

        prefix = g_strdup_printf("%s.%zu", key, i);

        if (virQEMUBuildCommandLineJSONRecurse(prefix, member, buf, skipKey, onOff,
                                               virQEMUBuildCommandLineJSONArrayNumbered,
                                               true) < 0)
            return 0;
    }

    return 0;
}


/**
 * This array convertor is for quirky cases where the QMP schema mandates an
 * array of objects with only one attribute 'str' which needs to be formatted as
 * repeated key-value pairs without the 'str' being printed:
 *
 * 'guestfwd': [
 *                  { "str": "tcp:10.0.2.1:4600-chardev:charchannel0" },
 *                  { "str": "...."},
 *             ]
 *
 *  guestfwd=tcp:10.0.2.1:4600-chardev:charchannel0,guestfwd=...
 */
static int
virQEMUBuildCommandLineJSONArrayObjectsStr(const char *key,
                                           virJSONValuePtr array,
                                           virBufferPtr buf,
                                           const char *skipKey G_GNUC_UNUSED,
                                           bool onOff G_GNUC_UNUSED)
{
    g_auto(virBuffer) tmp = VIR_BUFFER_INITIALIZER;
    size_t i;

    for (i = 0; i < virJSONValueArraySize(array); i++) {
        virJSONValuePtr member = virJSONValueArrayGet(array, i);
        const char *str = virJSONValueObjectGetString(member, "str");

        if (!str)
            return -1;

        virBufferAsprintf(&tmp, "%s=%s,", key, str);
    }

    virBufferAddBuffer(buf, &tmp);
    return 0;
}


/* internal iterator to handle nested object formatting */
static int
virQEMUBuildCommandLineJSONIterate(const char *key,
                                   virJSONValuePtr value,
                                   void *opaque)
{
    struct virQEMUCommandLineJSONIteratorData *data = opaque;

    if (STREQ_NULLABLE(key, data->skipKey))
        return 0;

    if (data->prefix) {
        g_autofree char *tmpkey = NULL;

        tmpkey = g_strdup_printf("%s.%s", data->prefix, key);

        return virQEMUBuildCommandLineJSONRecurse(tmpkey, value, data->buf,
                                                  data->skipKey, data->onOff,
                                                  data->arrayFunc, false);
    } else {
        return virQEMUBuildCommandLineJSONRecurse(key, value, data->buf,
                                                  data->skipKey, data->onOff,
                                                  data->arrayFunc, false);
    }
}


static int
virQEMUBuildCommandLineJSONRecurse(const char *key,
                                   virJSONValuePtr value,
                                   virBufferPtr buf,
                                   const char *skipKey,
                                   bool onOff,
                                   virQEMUBuildCommandLineJSONArrayFormatFunc arrayFunc,
                                   bool nested)
{
    struct virQEMUCommandLineJSONIteratorData data = { key, buf, skipKey, onOff, arrayFunc };
    virJSONType type = virJSONValueGetType(value);
    virJSONValuePtr elem;
    bool tmp;
    size_t i;

    if (!key && type != VIR_JSON_TYPE_OBJECT) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("only JSON objects can be top level"));
        return -1;
    }

    switch (type) {
    case VIR_JSON_TYPE_STRING:
        virBufferAsprintf(buf, "%s=", key);
        virQEMUBuildBufferEscapeComma(buf, virJSONValueGetString(value));
        virBufferAddLit(buf, ",");
        break;

    case VIR_JSON_TYPE_NUMBER:
        virBufferAsprintf(buf, "%s=%s,", key, virJSONValueGetNumberString(value));
        break;

    case VIR_JSON_TYPE_BOOLEAN:
        virJSONValueGetBoolean(value, &tmp);
        if (onOff) {
            if (tmp)
                virBufferAsprintf(buf, "%s=on,", key);
            else
                virBufferAsprintf(buf, "%s=off,", key);
        } else {
            if (tmp)
                virBufferAsprintf(buf, "%s=yes,", key);
            else
                virBufferAsprintf(buf, "%s=no,", key);
        }

        break;

    case VIR_JSON_TYPE_ARRAY:
        if (nested) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("nested JSON array to commandline conversion is "
                             "not supported"));
            return -1;
        }

        if (!arrayFunc || arrayFunc(key, value, buf, skipKey, onOff) < 0) {
            /* fallback, treat the array as a non-bitmap, adding the key
             * for each member */
            for (i = 0; i < virJSONValueArraySize(value); i++) {
                elem = virJSONValueArrayGet((virJSONValuePtr)value, i);

                /* recurse to avoid duplicating code */
                if (virQEMUBuildCommandLineJSONRecurse(key, elem, buf, skipKey,
                                                       onOff, arrayFunc, true) < 0)
                    return -1;
            }
        }
        break;

    case VIR_JSON_TYPE_OBJECT:
        if (virJSONValueObjectForeachKeyValue(value,
                                              virQEMUBuildCommandLineJSONIterate,
                                              &data) < 0)
            return -1;
        break;

    case VIR_JSON_TYPE_NULL:
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("NULL JSON type can't be converted to commandline"));
        return -1;
    }

    return 0;
}


/**
 * virQEMUBuildCommandLineJSON:
 * @value: json object containing the value
 * @buf: otuput buffer
 * @skipKey: name of key that will be handled separately by caller
 * @onOff: Use 'on' and 'off' for boolean values rather than 'yes' and 'no'
 * @arrayFunc: array formatter function to allow for different syntax
 *
 * Formats JSON value object into command line parameters suitable for use with
 * qemu.
 *
 * Returns 0 on success -1 on error.
 */
int
virQEMUBuildCommandLineJSON(virJSONValuePtr value,
                            virBufferPtr buf,
                            const char *skipKey,
                            bool onOff,
                            virQEMUBuildCommandLineJSONArrayFormatFunc array)
{
    if (virQEMUBuildCommandLineJSONRecurse(NULL, value, buf, skipKey, onOff, array, false) < 0)
        return -1;

    virBufferTrim(buf, ",");

    return 0;
}


/**
 * virQEMUBuildNetdevCommandlineFromJSON:
 * @props: JSON properties describing a netdev
 * @rawjson: don't transform to commandline args, but just stringify json
 *
 * Converts @props into arguments for -netdev including all the quirks and
 * differences between the monitor and command line syntax.
 *
 * @rawjson is meant for testing of the schema in the xml2argvtest
 */
char *
virQEMUBuildNetdevCommandlineFromJSON(virJSONValuePtr props,
                                      bool rawjson)
{
    const char *type = virJSONValueObjectGetString(props, "type");
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    if (rawjson)
        return virJSONValueToString(props, false);

    virBufferAsprintf(&buf, "%s,", type);

    if (virQEMUBuildCommandLineJSON(props, &buf, "type", true,
                                    virQEMUBuildCommandLineJSONArrayObjectsStr) < 0)
        return NULL;

    return virBufferContentAndReset(&buf);
}


char *
virQEMUBuildDriveCommandlineFromJSON(virJSONValuePtr srcdef)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    char *ret = NULL;

    if (virQEMUBuildCommandLineJSON(srcdef, &buf, NULL, false,
                                    virQEMUBuildCommandLineJSONArrayNumbered) < 0)
        goto cleanup;

    ret = virBufferContentAndReset(&buf);

 cleanup:
    virBufferFreeAndReset(&buf);
    return ret;
}


/**
 * virQEMUBuildBufferEscapeComma:
 * @buf: buffer to append the escaped string
 * @str: the string to escape
 *
 * qemu requires that any values passed on the command line which contain
 * a ',' must escape it using an extra ',' as the escape character
 */
void
virQEMUBuildBufferEscapeComma(virBufferPtr buf, const char *str)
{
    virBufferEscape(buf, ',', ",", "%s", str);
}


/**
 * virQEMUBuildQemuImgKeySecretOpts:
 * @buf: buffer to build the string into
 * @encinfo: pointer to encryption info
 * @alias: alias to use
 *
 * Generate the string for id=$alias and any encryption options for
 * into the buffer.
 *
 * Important note, a trailing comma (",") is built into the return since
 * it's expected other arguments are appended after the id=$alias string.
 * So either turn something like:
 *
 *     "key-secret=$alias,"
 *
 * or
 *     "key-secret=$alias,cipher-alg=twofish-256,cipher-mode=cbc,
 *     hash-alg=sha256,ivgen-alg=plain64,igven-hash-alg=sha256,"
 *
 */
void
virQEMUBuildQemuImgKeySecretOpts(virBufferPtr buf,
                                 virStorageEncryptionInfoDefPtr encinfo,
                                 const char *alias)
{
    virBufferAsprintf(buf, "key-secret=%s,", alias);

    if (!encinfo->cipher_name)
        return;

    virBufferAddLit(buf, "cipher-alg=");
    virQEMUBuildBufferEscapeComma(buf, encinfo->cipher_name);
    virBufferAsprintf(buf, "-%u,", encinfo->cipher_size);
    if (encinfo->cipher_mode) {
        virBufferAddLit(buf, "cipher-mode=");
        virQEMUBuildBufferEscapeComma(buf, encinfo->cipher_mode);
        virBufferAddLit(buf, ",");
    }
    if (encinfo->cipher_hash) {
        virBufferAddLit(buf, "hash-alg=");
        virQEMUBuildBufferEscapeComma(buf, encinfo->cipher_hash);
        virBufferAddLit(buf, ",");
    }
    if (!encinfo->ivgen_name)
        return;

    virBufferAddLit(buf, "ivgen-alg=");
    virQEMUBuildBufferEscapeComma(buf, encinfo->ivgen_name);
    virBufferAddLit(buf, ",");

    if (encinfo->ivgen_hash) {
        virBufferAddLit(buf, "ivgen-hash-alg=");
        virQEMUBuildBufferEscapeComma(buf, encinfo->ivgen_hash);
        virBufferAddLit(buf, ",");
    }
}


int
virQEMUFileOpenAs(uid_t fallback_uid,
                  gid_t fallback_gid,
                  bool dynamicOwnership,
                  const char *path,
                  int oflags,
                  bool *needUnlink)
{
    struct stat sb;
    bool is_reg = true;
    bool need_unlink = false;
    unsigned int vfoflags = 0;
    int fd = -1;
    int path_shared = virFileIsSharedFS(path);
    uid_t uid = geteuid();
    gid_t gid = getegid();

    /* path might be a pre-existing block dev, in which case
     * we need to skip the create step, and also avoid unlink
     * in the failure case */
    if (oflags & O_CREAT) {
        need_unlink = true;

        /* Don't force chown on network-shared FS
         * as it is likely to fail. */
        if (path_shared <= 0 || dynamicOwnership)
            vfoflags |= VIR_FILE_OPEN_FORCE_OWNER;

        if (stat(path, &sb) == 0) {
            /* It already exists, we don't want to delete it on error */
            need_unlink = false;

            is_reg = !!S_ISREG(sb.st_mode);
            /* If the path is regular file which exists
             * already and dynamic_ownership is off, we don't
             * want to change its ownership, just open it as-is */
            if (is_reg && !dynamicOwnership) {
                uid = sb.st_uid;
                gid = sb.st_gid;
            }
        }
    }

    /* First try creating the file as root */
    if (!is_reg) {
        if ((fd = open(path, oflags & ~O_CREAT)) < 0) {
            fd = -errno;
            goto error;
        }
    } else {
        if ((fd = virFileOpenAs(path, oflags, S_IRUSR | S_IWUSR, uid, gid,
                                vfoflags | VIR_FILE_OPEN_NOFORK)) < 0) {
            /* If we failed as root, and the error was permission-denied
               (EACCES or EPERM), assume it's on a network-connected share
               where root access is restricted (eg, root-squashed NFS). If the
               qemu user is non-root, just set a flag to
               bypass security driver shenanigans, and retry the operation
               after doing setuid to qemu user */
            if ((fd != -EACCES && fd != -EPERM) || fallback_uid == geteuid())
                goto error;

            /* On Linux we can also verify the FS-type of the directory. */
            switch (path_shared) {
                case 1:
                    /* it was on a network share, so we'll continue
                     * as outlined above
                     */
                    break;

                case -1:
                    virReportSystemError(-fd, oflags & O_CREAT
                                         ? _("Failed to create file "
                                             "'%s': couldn't determine fs type")
                                         : _("Failed to open file "
                                             "'%s': couldn't determine fs type"),
                                         path);
                    goto cleanup;

                case 0:
                default:
                    /* local file - log the error returned by virFileOpenAs */
                    goto error;
            }

            /* If we created the file above, then we need to remove it;
             * otherwise, the next attempt to create will fail. If the
             * file had already existed before we got here, then we also
             * don't want to delete it and allow the following to succeed
             * or fail based on existing protections
             */
            if (need_unlink)
                unlink(path);

            /* Retry creating the file as qemu user */

            /* Since we're passing different modes... */
            vfoflags |= VIR_FILE_OPEN_FORCE_MODE;

            if ((fd = virFileOpenAs(path, oflags,
                                    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
                                    fallback_uid, fallback_gid,
                                    vfoflags | VIR_FILE_OPEN_FORK)) < 0) {
                virReportSystemError(-fd, oflags & O_CREAT
                                     ? _("Error from child process creating '%s'")
                                     : _("Error from child process opening '%s'"),
                                     path);
                goto cleanup;
            }
        }
    }
 cleanup:
    if (needUnlink)
        *needUnlink = need_unlink;
    return fd;

 error:
    virReportSystemError(-fd, oflags & O_CREAT
                         ? _("Failed to create file '%s'")
                         : _("Failed to open file '%s'"),
                         path);
    goto cleanup;
}
