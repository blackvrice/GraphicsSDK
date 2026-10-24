import pathlib
source=pathlib.Path(__file__).with_name('build-consumers.py').read_text()
source=source.replace("v/(name+'.log')","v/(name+'-v2.log')").replace("v/'consumer-build-commands.json'","v/'consumer-build-commands-v2.json'").replace("folder='out/vulkan-pool-churn-'+variant","folder='out/vulkan-pool-churn-'+variant+'-v2'")
exec(compile(source,str(__file__),'exec'))
