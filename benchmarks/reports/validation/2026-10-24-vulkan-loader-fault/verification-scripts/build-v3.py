import pathlib
source=pathlib.Path(__file__).with_name('build.py').read_text()
source=source.replace("v/(name+'.log')","v/(name+'-v3.log')").replace("v/'build-commands.json'","v/'build-commands-v3.json'").replace("folder='out/vulkan-loader-fault-'+variant","folder='out/vulkan-loader-fault-'+variant+'-v3'")
exec(compile(source,str(__file__),'exec'))
