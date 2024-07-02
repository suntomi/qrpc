ssrc, rid, cname

cnameがおそらくtrackidに対応すると思われる。
cnameにはそれに所属するいくつかのssrcがある(eg. simulcast時の解像度/fps違い)。
ridがそれぞれどのRtpStreamがどの解像度に当たるか、を示している。
cname->ridの紐付けは、cnameからssrcがわかり、そのssrcのrtp streamに対し、extensionのridからわかる
つまり、rtp-streamのrid -> 対応するssrc -> cname となる。はず
