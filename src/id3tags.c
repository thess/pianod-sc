/*
Copyright (c) 2019
	Ted Hess <thess@kitschensync.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <id3tag.h>
#include "player.h"

#define TAG_PADDED_SIZE 1024

union id3_field *ID3FindField(struct id3_frame *frame, enum id3_field_type ftype)
{
    union id3_field *field = NULL;

    int index = 0;
    do {
        field = id3_frame_field(frame, index);
        if (field == 0) {
            flog(LOG_ERROR, "Cannot find field: %d\n", ftype);
            return NULL;
        }
        index++;
    } while (id3_field_type(field) != ftype);

    return field;
};

int ID3AddGainFrame(struct id3_tag* tags, float value)
{
    int status;
    struct id3_frame* frame = NULL;
    union id3_field* field;
    id3_byte_t RGData[5];

    /*
     * Create the frame.
     */
    frame = id3_frame_new("RVA2");
    if (frame == NULL) {
        flog(LOG_ERROR, "Failed to create new frame (type = RVA2).\n");
        return -1;
    }

    frame->flags &= ~ID3_FRAME_FLAG_FORMATFLAGS;

    // Add identification
    field = ID3FindField(frame, ID3_FIELD_TYPE_LATIN1);
    if (field == NULL) {
        id3_frame_delete(frame);
        return -1;
    }

    status = id3_field_setlatin1(field, (id3_latin1_t *)"PandoraRG");
    if (status != 0) {
        flog(LOG_ERROR, "Failed to set RG ID.\n");
        id3_frame_delete(frame);
        return -1;
    }

    field = ID3FindField(frame, ID3_FIELD_TYPE_BINARYDATA);
    if (field == NULL) {
        id3_frame_delete(frame);
        return -1;
    }

    // Set channel to master volume (0x01)
    // Set gain to int16(DB * 512), peak to 0x00
    memset(RGData, 0, sizeof(RGData));
    RGData[0] = 0x01;
    *(int16_t *)&RGData[1] = htobe16((int16_t)(value * 512.0));
    status = id3_field_setbinarydata(field, RGData, sizeof(RGData));
    if (status != 0) {
        flog(LOG_ERROR, "Failed to set RG data.\n");
        id3_frame_delete(frame);
        return -1;
    }

    // Attach the frame to the tag.
    status = id3_tag_attachframe(tags, frame);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to attach frame (type = RVA2).\n");
        id3_frame_delete(frame);
        return -1;
    }

    return 0;
}

int ID3AddCommentFrame(struct id3_tag* tags, id3_utf8_t const* value)
{
    int status;
    struct id3_frame* frame = NULL;
    union id3_field* field;
    id3_ucs4_t* ucs4 = NULL;

    /*
     * Create the frame.
     */
    frame = id3_frame_new(ID3_FRAME_COMMENT);
    if (frame == NULL) {
        flog(LOG_ERROR, "Failed to create new frame (type = COMM).\n");
        return -1;
    }

    frame->flags &= ~ID3_FRAME_FLAG_FORMATFLAGS;

    field = ID3FindField(frame, ID3_FIELD_TYPE_LANGUAGE);
    if (field == NULL) {
        id3_frame_delete(frame);
        return -1;
    }

    status = id3_field_setlanguage(field, "ENG");
    if (status != 0) {
        flog(LOG_ERROR, "Failed to set field language.\n");
        id3_frame_delete(frame);
        return -1;
    }

     // Get the string list field of the frame.
    field = ID3FindField(frame, ID3_FIELD_TYPE_STRINGFULL);
    if (field == NULL) {
        id3_frame_delete(frame);
        return -1;
    }

    ucs4 = id3_utf8_ucs4duplicate(value);
    if (ucs4 == NULL) {
        flog(LOG_ERROR, "ucs4 dup error.\n");
        id3_frame_delete(frame);
        return -1;
    }

    status = id3_field_setfullstring(field, ucs4);
    free(ucs4);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to set field value (value = %s).\n", value);
        id3_frame_delete(frame);
        return -1;
    }

    /*
     * Attach the frame to the tag.
     */
    status = id3_tag_attachframe(tags, frame);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to attach frame (type = COMM).\n");
        id3_frame_delete(frame);
        return -1;
    }

    return 0;
}

int ID3AddTextFrame(struct id3_tag* tags, char const* type, id3_utf8_t const* value)
{
    int status;
    struct id3_frame* frame = NULL;
    union id3_field* field;
    id3_ucs4_t* ucs4 = NULL;

    /*
     * Create the frame.
     */
    frame = id3_frame_new(type);
    if (frame == NULL) {
        flog(LOG_ERROR, "Failed to create new frame (type = %s).\n", type);
        return -1;
    }

    frame->flags &= ~ID3_FRAME_FLAG_FORMATFLAGS;

     // Get the string list field of the frame.
    field = ID3FindField(frame, ID3_FIELD_TYPE_STRINGLIST);
    if (field == NULL) {
        id3_frame_delete(frame);
        return -1;
    }

    ucs4 = id3_utf8_ucs4duplicate(value);
    if (ucs4 == NULL) {
        flog(LOG_ERROR, "ucs4 dup error.\n");
        id3_frame_delete(frame);
        return -1;
    }

    status = id3_field_addstring(field, ucs4);
    free(ucs4);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to set field value (value = %s).\n", value);
        id3_frame_delete(frame);
        return -1;
    }

    /*
     * Attach the frame to the tag.
     */
    status = id3_tag_attachframe(tags, frame);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to attach frame (type = %s).\n",	type);
        id3_frame_delete(frame);
        return -1;
    }

    return 0;
}

int ID3WriteTags(struct audioPlayer *player, PianoSong_t *song, char *station_name)
{
    int status;
    id3_length_t size1;
    id3_length_t size2;
    id3_byte_t* tag_buffer = NULL;

    struct id3_tag *tags = id3_tag_new();
    if (!tags) {
        flog(LOG_ERROR, "Failed to create new tag.\n");
        return 1;
    }

    // Add extra space to allow editing (not required)
    id3_tag_setlength(tags, TAG_PADDED_SIZE);

    id3_tag_options(tags,
            ID3_TAG_OPTION_UNSYNCHRONISATION |
            ID3_TAG_OPTION_APPENDEDTAG |
            ID3_TAG_OPTION_CRC |
            ID3_TAG_OPTION_COMPRESSION, 0);

    // Add some data to the tag

    status = ID3AddTextFrame(tags, ID3_FRAME_TITLE, (id3_utf8_t *)song->title);
    status += ID3AddTextFrame(tags, ID3_FRAME_ARTIST, (id3_utf8_t *)song->artist);
    status += ID3AddTextFrame(tags, ID3_FRAME_ALBUM, (id3_utf8_t *)song->album);
    if (station_name) {
        status += ID3AddCommentFrame(tags, (id3_utf8_t *)station_name);
    }
    status += ID3AddGainFrame(tags, song->fileGain);
    if (status != 0) {
        flog(LOG_ERROR, "Failed to add frames to tags.\n");
        id3_tag_delete(tags);
        return 1;
    }

    // Add tags to empty file

    size1 = id3_tag_render(tags, NULL);
    tag_buffer = malloc(size1);
    if (tag_buffer == NULL) {
        flog(LOG_ERROR, "Failed to allocate memory (bytes = %li).\n", size1);
        id3_tag_delete(tags);
        return 1;
    }

    size2 = id3_tag_render(tags, tag_buffer);
    if (size1 != size2) {
        flog(LOG_ERROR, "Invalid tag size (expected = %li, received = %li).\n", size1, size2);
        free(tag_buffer);
        id3_tag_delete(tags);
        return 1;
    }

    int nc = fwrite(tag_buffer, sizeof(id3_byte_t), size2, player->capture_file);

    // Cleanup
    free(tag_buffer);
    id3_tag_delete(tags);

    if (nc != size2) {
        flog(LOG_ERROR, "Tag write error (%d) %s\n", errno, strerror(errno));
        return 1;
    }

    return 0;
}
