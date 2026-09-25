args <- commandArgs(trailingOnly=FALSE)
file <- sub("^--file=","",args[grepl("^--file=",args)][1])
app_dir <- dirname(normalizePath(file))
needed <- c("shiny","jsonlite","curl")
missing <- needed[!vapply(needed,requireNamespace,logical(1),quietly=TRUE)]
if(length(missing)) stop("Install required packages first: install.packages(c(",paste(sprintf('"%s"',missing),collapse=","),"))")
options(shiny.autoreload=FALSE)
shiny::runApp(app_dir,host="127.0.0.1",port=as.integer(Sys.getenv("MESS_DASHBOARD_PORT","3838")),launch.browser=FALSE)
