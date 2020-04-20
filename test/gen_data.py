import struct

def value(n):
    return bytearray(struct.pack("<f", float(n)))

with open("../test.bin", "wb") as bf:
    id_ = 0
    iq_ = 0
    speed = 500
    ld = 0.01
    lq = 0.02
    Rs = 0.001
    lamb = 0.003
    temp = 80
    for row in range(8000000):
        bf.write(value(id_))
        bf.write(value(iq_))
        bf.write(value(speed))
        bf.write(value(id_*0.1 + iq_*11))
        bf.write(value(ld))
        bf.write(value(lq))
        bf.write(value(Rs))
        bf.write(value(lamb))
        bf.write(value(temp))

        if iq_ == 7:
            id_ += 1
            iq_ = 0
        else:
            iq_ += 1

        if id_ > 1000:
            id_ = 0

