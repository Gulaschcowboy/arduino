/* -*- coding: utf-8 -*-

BERadio: Convenient and flexible telemetry messaging for Hiveeyes

Copyright (C) 2015-2016  Andreas Motl <andreas.motl@elmyra.de>
Copyright (C) 2015-2016  Richard Pobering <einsiedlerkrebs@ginnungagap.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see:
<http://www.gnu.org/licenses/gpl-3.0.txt>,
or write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

*/
#include <Terrine.h>
#include <BERadio.h>
/*
#include <simulavr.h>
#if HE_ARDUINO
#include <Arduino.h>
#else
#include <RHutil/simulator.h>
#endif
*/

void BERadioEncoder::reset() {
    length = 0;
}

void BERadioEncoder::PushChar(char ch) {

    // Discard overflow data. This is bad.
    if (length >= MTU_SIZE_MAX) {
        //_d("Payload limit reached, aborting serialization");
        return;
    }

    buffer[length] = ch;
    length += 1;
}

/*
void BERadioShadowEncoder::PushChar(char ch) {

    // Discard overflow data. This is bad.
    if (length >= 10) {
        //_d("Payload limit reached, aborting serialization");
        return;
    }

    buffer[length] = ch;
    length += 1;
}
*/

void BERadioMessage::set_mtu_size(int size) {
    // Set maximum transfer unit (MTU) size
    mtu_size = size;
}

void BERadioMessage::add(char *family, std::vector<double> &values) {
    // Store list of values into map, keyed by single char family identifier (t, h, w, r)
    _store[family] = values;
}

void BERadioMessage::transmit() {

    // Encoder machinery wrapping EmBencode
    // Main message encoder
    BERadioEncoder *encoder = new BERadioEncoder();

    // Shadow encoder for serializing single elements
    BERadioEncoder *shadow = new BERadioEncoder();

    // Initialize message, add header
    start_message(*encoder);

    // Encode internal data store
    bool do_fragment = false;

    // Iterate data store mapping single-char family identifiers to value lists
    for (auto iterator = _store.begin(); iterator != _store.end(); iterator++) {

        // iterator->first  = key
        // iterator->second = value

        // Decode family identifier and list of values from map element (key/value pair)
        char *family                = iterator->first;
        std::vector<double> &values = iterator->second;

        // Number of elements in value list
        int length = values.size();

        // Skip empty lists
        if (length == 0) {
            continue;
        }


        // Encode the family identifier of this value list
        encoder->push(family);

        // Encode list of values, apply forward-scaling by * 100

        // Fast-path: Compress lists with single elements
        // TODO: Also care about fragmentation here, currently disabled
        if (false && length == 1) {
            // List compression: Lists with just one element don't
            // need to be wrapped into Bencode list containers.
            long real_value = (long)(values[0] * 100);
            encoder->push(real_value);

        } else {

            // Lists with two or more elements
            encoder->startList();

            // Iterate list of measurement values
            for (unsigned long index = 0; index < values.size(); index++) {

                // Decode element
                double value = values[index];
                long real_value = (long)(value * 100);

                // Simulate Bencode serialization to compute length of encoded element
                shadow->reset();
                shadow->push(real_value);

                // Compute whether message should be fragmented right here
                int close_padding = 2;       // Two levels of nestedness: dict / list
                int current_size = encoder->length + shadow->length + close_padding;
                do_fragment = (bool)(current_size >= mtu_size);

                if (do_fragment) {

                    dprint("fL");

                    // Close current list
                    encoder->endList();

                    // Send out data
                    fragment_and_send(*encoder);

                    // Start new message
                    start_message(*encoder);

                    // Open new list context where we currently left off
                    dprint("fLc");
                    continue_list(*encoder, family, index);

                }

                // Just push if it's safe
                encoder->push(real_value);

                // Refresh "do_fragment" state
                // TODO: This must be run here, but the source code is redundant. => Refactor to function.
                do_fragment = (bool)((encoder->length + shadow->length + close_padding) >= mtu_size);

            }
            encoder->endList();

        }

        // End of family values
        if (do_fragment) {

            // Send out data
            fragment_and_send(*encoder);

            // Start new message
            start_message(*encoder);
        }

    }

    // Regular ending
    if (!do_fragment) {
        // Send out data
        fragment_and_send(*encoder);
    }

    delete encoder;
    delete shadow;

    // Ready.
}

void BERadioMessage::start_message(BERadioEncoder &encoder) {

    encoder.reset();

    // Open envelope
    encoder.startDict();

    // Add node identifier (integer)
    encoder.push("#");
    encoder.push(nodeid);

    // Add profile identifier (string)
    encoder.push("_");
    encoder.push(profile.c_str());

}

void BERadioMessage::fragment_and_send(BERadioEncoder &encoder) {

    // Close envelope
    encoder.endDict();

    // Convert character buffer of known length to standard string
    // TODO: Check if we can use std::string alternatively
    //std::string payload(encoder.buffer, encoder.length);
    //std::string payload(encoder.buffer, encoder.length);
    //std::string *payload = new std::string(encoder.buffer, encoder.length);

    //dprint(payload.c_str());
    //delete payload;

    // Debugging
    //_l("payload: "); _d(payload);

    // Transmit message before starting with new one
    encoder.buffer[encoder.length] = '\0';
    send(encoder.buffer, encoder.length);
    //dprint(payload->c_str());

    //delete payload;

}

void BERadioMessage::continue_list(BERadioEncoder &encoder, std::string family, int index) {

    // Augment family identifier with current index, e.g. "t3"

    // FIXME: How to convert from Integer to String more conveniently?
    char intbuf[3];
    std::sprintf(intbuf, "%d", index);

    std::string family_with_index = family + std::string(intbuf);

    // Reopen list

    // Encode the augmented family identifier of this value list
    encoder.push(family_with_index.c_str());

    // Restart list container
    encoder.startList();
}


void BERadioMessage::debug(bool enabled) {
    DEBUG = enabled;
    //_l("Node id: "); _d(nodeid);
    //_l("Profile: "); _d(profile);
}
