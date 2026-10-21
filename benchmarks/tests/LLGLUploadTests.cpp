#include "common/Adapter.hpp"
#include "common/UploadLayout.hpp"
#include <array>
#include <algorithm>
#include <iostream>

int main() try {
    unsigned checks=0;
    auto check=[&](bool ok) { ++checks; if (!ok) throw std::runtime_error("LLGL upload contract failed"); };
    for (bool half:{false,true}) {
        for (auto extent:std::array<std::array<unsigned,2>,4>{{{7,5},{257,3},{1920,1080},{3840,2160}}}) {
            const auto layout=bench::uploadLayout(extent[0],extent[1],half);
            check(layout.rowPitch%256==0 && layout.rowPitch>=layout.rowBytes);
            std::vector<std::byte> source(static_cast<size_t>(layout.tightSize),std::byte{0x7f});
            std::vector<std::byte> padded(static_cast<size_t>(layout.size),std::byte{0x55});
            bench::padUploadRows(padded,source,layout);
            for (unsigned y=0;y<layout.height;++y) {
                const auto offset=static_cast<size_t>(y)*layout.rowPitch;
                check(std::equal(source.begin()+static_cast<size_t>(y)*layout.rowBytes,source.begin()+static_cast<size_t>(y+1)*layout.rowBytes,padded.begin()+offset));
                check(std::all_of(padded.begin()+offset+layout.rowBytes,padded.begin()+offset+layout.rowPitch,[](auto b){return b==std::byte{0};}));
            }
            auto adapter=bench::llgl(true);
            check(adapter->uploadModel()==bench::llglUploadModel);
            adapter->prepareUpload(extent[0],extent[1],half);
            for (unsigned frame=0;frame<6;++frame) {
                for (size_t i=0;i<source.size();++i)
                    source[i]=half?static_cast<std::byte>(i%2?0x38+((i/2+frame)%3)*2:0):static_cast<std::byte>((i*17+frame*47)%251);
                adapter->upload(source);
                if (frame%2==0) adapter->upload(source);
                check(adapter->verifyUpload(source));
                bool rejected=false;
                try { adapter->upload(std::span<const std::byte>(source).first(source.size()-1)); } catch (const std::runtime_error&) { rejected=true; }
                check(rejected); check(adapter->verifyUpload(source));
            }
        }
    }
    for (auto extent:std::array<std::array<unsigned,2>,4>{{{0,1},{1,0},{UINT32_MAX,1},{16384,UINT32_MAX}}}) {
        bool rejected=false; try { bench::uploadLayout(extent[0],extent[1],true); } catch(const std::runtime_error&) { rejected=true; }
        check(rejected);
    }
    std::cout<<"PASS: "<<checks<<" checks; 8 format/extent cases, 72 public uploads, 96 full readbacks, native validation through teardown\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
