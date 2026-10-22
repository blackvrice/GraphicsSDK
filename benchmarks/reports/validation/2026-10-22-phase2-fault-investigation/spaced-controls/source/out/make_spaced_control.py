import pathlib
root=pathlib.Path.cwd();source=(root/'out/gbv-retention-control/main.cpp').read_text()
anchor='        for(unsigned second=1;second<=20;++second){Sleep(1000);sample("alive_quiet");}\n'
assert anchor in source;source=source.replace(anchor,'')
anchor='sample("without_heap_trim");check(privateBytes()<1024*MiB,"Causal diagnostic budget exceeded");'
assert anchor in source
source=source.replace(anchor,anchor+'\n            for(unsigned second=1;second<=20;++second){Sleep(1000);sample("between_batch_quiet");}')
folder=root/'out/gbv-spaced-control';folder.mkdir(exist_ok=False)
(folder/'main.cpp').write_text(source)
(folder/'CMakeLists.txt').write_text((root/'out/gbv-retention-control/CMakeLists.txt').read_text().replace('gbv_retention_control','gbv_spaced_control').replace('GBVRetentionControl','GBVSpacedControl'))
