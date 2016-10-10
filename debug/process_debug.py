def process_debug(i="debug.txt", o="debug.html"):
    fi = open(i)
    fo = open(o, "w")

    ms_px_mult = 1000
    time_offset = 100000000
    total_sched = 0
    total_exec = 0
    total_rdtscp_s = 0
    total_rdtscp_e = 0
    n_tasks = 0

    fo.write("<html>\n\t<head>\n")
    fo.write("\t\t<link rel=\"stylesheet\" type=\"text/css\" href=\"debug.css\"></head>\n\t<body>\n")

    fo.write("\t\t<div class=\"thread-lane\">\n")

    for line in fi:
        if line.startswith("THREAD"):
            fo.write("\t\t</div>\n")
            fo.write("\t\t<div class=\"thread-lane\">\n")
        elif "----" in line:
            pass
        else:
            ss, se, ee, rdtscp_s, rdtscp_e, s, scp_prev, scp_curr = [ float(x.strip()) for x in line.split("|") ]
            s, scp_prev, scp_curr = [ int(x) for x in [ s, scp_prev, scp_curr ] ]
            if ss < time_offset:
                time_offset = ss
            total_sched += (se - ss)
            total_exec += (ee - se)
            total_rdtscp_s += rdtscp_s
            total_rdtscp_e += rdtscp_e
            if ss:
                n_tasks += 1
                fo.write("\t\t\t<div class=\"sched\" style=\"left: {0}px; width: {1}px;\"></div>\n".format((ss-time_offset)*ms_px_mult, (se-ss)*ms_px_mult))
                fo.write("\t\t\t<div class=\"exec stack-{0}\" style=\"left: {1}px; width: {2}px;\">{3} {4}</div>\n".format(s, (se-time_offset)*ms_px_mult, (ee-se)*ms_px_mult, scp_prev, scp_curr))

    fo.write("\t\t</div>\n")

    fo.write("\t\t<div class=\"legend\">\n")

    fo.write("\t\t\t<span class=\"time-stats\">total sched (avg.): {0} ({1}) total exec (avg.): {2} ({3}) sched/exec: {4}</span>\n".format(total_sched, total_sched / n_tasks, total_exec, total_exec / n_tasks, total_sched / total_exec))
    fo.write("\t\t\t<br />\n")
    fo.write("\t\t\t<span class=\"time-stats\">total rdtscp_s (avg.): {0} ({1}) total rdtscp_e (avg.): {2} ({3}) rdtscp_s/rdtscp_e: {4}</span>\n".format(total_rdtscp_s, total_rdtscp_s / n_tasks, total_rdtscp_e, total_rdtscp_e / n_tasks, total_rdtscp_s / total_rdtscp_e))

    for s in range(0, 8):
        fo.write("\t\t\t<div class=\"stack-{0}\">stack {0}</div>\n".format(s))

    fo.write("\t\t</div>\n")

    fo.write("\t</body>\n</html>")

    fi.close()
    fo.close()
