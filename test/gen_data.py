def value(n):
    return int(n).to_bytes(2, byteorder='little', signed=True)

with open("../test.bin", "wb") as bf:
    header = ("id", "iq", "speed", "torque")
    id_ = 0
    iq_ = 0
    speed = 500
    for row in range(32):
        bf.write(value(id_))
        bf.write(value(iq_))
        bf.write(value(speed))
        bf.write(value(id_*0.1 + iq_*11))

        if iq_ == 7:
            id_ += 1
            iq_ = 0
        else:
            iq_ += 1

