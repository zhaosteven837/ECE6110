import concurrent.futures
import subprocess

file = "radiiTest.txt"

#nDevices = ['20', '40', '60', '80', '100', '120', '140', '160', '180', '200']
nDevices = ['100']
#packetDelays = ['1', '5', '10', '15', '20', '25', '30', '35', '40', '45', '50', '55', '60', '65', '70', '75', '80', '85', '90', '95', '100']
packetDelays = ['100']
simulationTime = ['5']
#radii = ['1000']
radii = ['1000', '5000', '10000', '15000', '20000', '25000']
#packetSizes = ['5', '10', '50', '100']
packetSizes = ['50']
#dataRates = ['5']
dataRates = ['3', '4', '5']
futures = []

def runScript(nDevices, packetDelay, simulationTime, radius, packetSize, dataRate):
    returnThing = subprocess.run(['./ns3', 'run', '\"scratch/Final_Project.cc --nDevices=' + nDevices + ' --packetDelay=' + packetDelay + ' --simulationTime=' + simulationTime + ' --radius=' + radius + ' --packetSize=' + packetSize + ' --dataRate=' + dataRate + '\"'], stdout=subprocess.PIPE)
    try:
        ipc = nDevices + ", " + packetDelay + ", " + simulationTime + ", " + radius + ", " + packetSize + ", " + dataRate + ", " + returnThing.stdout.decode('utf-8').splitlines()[0].split()[2] + ", " + returnThing.stdout.decode('utf-8').splitlines()[1].split()[4] + ", " + returnThing.stdout.decode('utf-8').splitlines()[1].split()[5] + "\n"
    except:
        print("error on config: " + "nDevices: " + nDevices + ", packetDelay: " + packetDelay + ", simulationTime: " + simulationTime + ", radius: " + radius + ", packetSize: " + packetSize + ", dataRate: " + dataRate)
        print(returnThing.stdout.decode('utf-8') + "\n")
        ipc = "error"
    return ipc

with open(file, "w") as myfile:
    myfile.write("nDevices, packetDelay, simulationTime, radius, packetSize, dataRate, energyConsumed, packetsSent, packetsReceived\n")

with concurrent.futures.ThreadPoolExecutor(max_workers=12) as executor:
    for nDevice in nDevices:
        for packetDelay in packetDelays:
            for simTime in simulationTime:
                for radius in radii:
                    for packetSize in packetSizes:
                        for dataRate in dataRates:
                            future=executor.submit(runScript, nDevice, packetDelay, simTime, radius, packetSize, dataRate)
                            futures.append(future)

    for future in concurrent.futures.as_completed(futures):
        with open(file, "a") as myfile:
            myfile.write(future.result())
