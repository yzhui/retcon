//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "util.h"
#include "hash.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include "libtwitcurl/SHA1.h"

std::string hexify(const char *in, size_t len) {
	const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(2 * len);
	for(size_t i = 0; i < len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.push_back(hex[c >> 4]);
		out.push_back(hex[c & 15]);
	}
	return out;
}

wxString hexify_wx(const char *in, size_t len) {
	const wxChar hex[] = wxT("0123456789ABCDEF");
	wxString out;
	out.Alloc(2 * len);
	for(size_t i = 0; i < len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.Append(hex[c >> 4]);
		out.Append(hex[c & 15]);
	}
	return out;
}

shb_iptr hash_block(const void *data, size_t length) {
	std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
	hash_block(*hash, data, length);
	return std::move(hash);
}

void hash_block(sha1_hash_block &out, const void *data, size_t length) {
	CSHA1 hashblk;
	hashblk.Update(static_cast<const unsigned char*>(data), length);
	hashblk.Final();
	hashblk.GetHash(static_cast<unsigned char*>(out.hash_sha1));
}

bool LoadFromFileAndCheckHash(const wxString &filename, shb_iptr hash, std::string &out) {
	if(!hash) return false;
	wxFile file;
	bool opened = file.Open(filename);
	if(opened) {
		wxFileOffset len = file.Length();
		if(len >= 0 && len < (50 << 20)) {    //don't load empty or absurdly large files
			out.resize(len);
			size_t size = file.Read(&out[0], len);
			if(size == (size_t) len) {
				CSHA1 hashblk;
				hashblk.Update(reinterpret_cast<const unsigned char*>(out.data()), len);
				hashblk.Final();
				if(memcmp(hashblk.GetHashPtr(), hash->hash_sha1, 20) == 0) {
					return true;
				}
			}
			out.clear();
		}
	}
	return false;
}

bool LoadImageFromFileAndCheckHash(const wxString &filename, shb_iptr hash, wxImage &img) {
	if(!hash) return false;
	std::string data;
	bool success = false;
	if(LoadFromFileAndCheckHash(filename, hash, data)) {
		wxMemoryInputStream memstream(data.data(), data.size());
		if(img.LoadFile(memstream, wxBITMAP_TYPE_ANY)) {
			success = true;
		}
	}
	return success;
}

std::string rc_strftime(const std::string &format, const struct tm *tm, time_t timestamp, bool localtime) {
	#ifdef __WINDOWS__
				//%z is broken in MSVCRT, use a replacement
				//also add %F, %R, %T, %s
				//this is adapted from npipe var.cpp
	std::string newfmt;
	newfmt.reserve(format.size());
	std::string &real_format = newfmt;
	const char *ch = format.c_str();
	const char *cur = ch;
	while(*ch) {
		if(ch[0] == '%') {
			std::string insert;
			if(ch[1] == 'z') {
				int hh;
				int mm;
				if(localtime) {
					TIME_ZONE_INFORMATION info;
					DWORD res = GetTimeZoneInformation(&info);
					int bias = - info.Bias;
					if(res == TIME_ZONE_ID_DAYLIGHT) bias -= info.DaylightBias;
					hh = bias / 60;
					if(bias < 0) bias =- bias;
					mm = bias % 60;
				}
				else {
					hh = mm = 0;
				}
				insert = string_format("%+03d%02d", hh, mm);
			}
			else if(ch[1] == 'F') {
				insert = "%Y-%m-%d";
			}
			else if(ch[1] == 'R') {
				insert = "%H:%M";
			}
			else if(ch[1] == 'T') {
				insert = "%H:%M:%S";
			}
			else if(ch[1] == 's') {
				insert = string_format("%" llFmtSpec "d", (long long int) timestamp);
			}
			else if(ch[1]) {
				ch++;
			}
			if(insert.length()) {
				real_format.insert(real_format.end(), cur, ch); // Add up to current point
				real_format += insert;
				cur = ch + 2; // Update point where next add will start from
			}
		}
		ch++;
	}
	real_format += cur; // Add remainder of string
	#else
	const std::string &real_format = format;
	#endif

	char timestr[256];
	strftime(timestr, sizeof(timestr), cstr(real_format), tm);
	return std::string(timestr);
}

//from http://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf#2342176
std::string string_format(const std::string &fmt, ...) {
    int size = 100;
    std::string str;
    va_list ap;
    while (1) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf((char *)str.c_str(), size, fmt.c_str(), ap);
        va_end(ap);
        if (n > -1 && n < size) {
            str.resize(n);
            return str;
        }
        if (n > -1)
            size = n + 1;
        else
            size *= 2;
    }
    return str;
}
