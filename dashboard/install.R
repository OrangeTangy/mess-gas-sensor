# Run from any directory: Rscript path/to/dashboard/install.R [--editor] [--tests]
args <- commandArgs(trailingOnly=TRUE)
packages <- c("shiny", "jsonlite", "curl")
if("--editor" %in% args) packages <- c(packages,"languageserver")
if("--tests" %in% args) packages <- c(packages,"testthat")
missing <- packages[!vapply(packages,requireNamespace,logical(1),quietly=TRUE)]
if(length(missing)) {
  if(file.access(.libPaths()[1],2)!=0) {
    user_lib <- path.expand(Sys.getenv("R_LIBS_USER"))
    if(!nzchar(user_lib)) stop("Set R_LIBS_USER to a writable R package library.")
    dir.create(user_lib,recursive=TRUE,showWarnings=FALSE)
    .libPaths(c(user_lib,.libPaths()))
  }
  install.packages(missing,repos="https://cloud.r-project.org")
}
remaining <- packages[!vapply(packages,requireNamespace,logical(1),quietly=TRUE)]
if(length(remaining)) stop("Installation incomplete: ",paste(remaining,collapse=", "))
cat("Dashboard dependencies ready. Start with: Rscript dashboard/run.R\n")
