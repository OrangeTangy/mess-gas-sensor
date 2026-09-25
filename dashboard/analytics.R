# Pure analytics functions; no Shiny dependency. Missing packets are estimated
# within each received sequence span. Trailing outages are displayed separately.
prepare_data <- function(d) {
  if (is.null(d) || !is.data.frame(d) || !nrow(d)) return(data.frame())
  d$received_at <- as.POSIXct(d$received_at, format="%Y-%m-%dT%H:%M:%OS", tz="UTC")
  d <- d[!is.na(d$received_at), , drop=FALSE]
  if (!nrow(d)) return(d)
  defaults <- list(transport="wifi", mesh_layer=0L, rssi_dbm=-127L,
                   node_role="sensor",trial_label="",distance_m=NA_real_,trial_notes="")
  for (key in names(defaults)) if (!key %in% names(d)) d[[key]] <- defaults[[key]]
  for (key in c("eco2_ppm","tvoc_ppb","sequence","uptime_ms","rssi_dbm","distance_m","mesh_layer"))
    d[[key]] <- as.numeric(d[[key]])
  d$valid <- tolower(as.character(d$valid))=="true"
  d <- d[!duplicated(paste(d$node_id,d$boot_id,d$sequence)),,drop=FALSE]
  d[order(d$received_at),,drop=FALSE]
}
span_delivery <- function(d) {
  if (!nrow(d)) return(list(received=0,expected=0,missing=0,pct=NA_real_))
  groups <- split(d, paste(d$node_id,d$boot_id))
  expected <- sum(vapply(groups,function(g) max(g$sequence)-min(g$sequence)+1,numeric(1)))
  received <- sum(vapply(groups,function(g) length(unique(g$sequence)),integer(1)))
  list(received=received,expected=expected,missing=expected-received,pct=100*received/expected)
}
node_summary <- function(d, now=Sys.time()) {
  if (!nrow(d)) return(data.frame())
  do.call(rbind,lapply(split(d,d$node_id),function(g) {
    latest <- tail(g,1); good <- g[g$valid & g$node_role=="sensor",,drop=FALSE]
    delivery <- span_delivery(g)
    age <- max(0,as.numeric(difftime(now,latest$received_at,units="secs")))
    data.frame(node=latest$node_id,role=latest$node_role,
      state=if(age>10) "OFFLINE" else if(latest$status=="sensor_error") "SENSOR ERROR" else toupper(latest$status),
      age_s=round(age,1),eco2_ppm=if(latest$valid) latest$eco2_ppm else NA_real_,
      tvoc_ppb=if(latest$valid) latest$tvoc_ppb else NA_real_,
      rssi_dbm=if(latest$rssi_dbm==-127) NA_real_ else latest$rssi_dbm,
      layer=latest$mesh_layer,delivery_pct=round(delivery$pct,1),missing=delivery$missing,
      mean_tvoc=if(nrow(good)) round(mean(good$tvoc_ppb,na.rm=TRUE),1) else NA_real_,
      min_tvoc=if(nrow(good)) min(good$tvoc_ppb,na.rm=TRUE) else NA_real_,
      max_tvoc=if(nrow(good)) max(good$tvoc_ppb,na.rm=TRUE) else NA_real_,stringsAsFactors=FALSE)
  }))
}
range_summary <- function(d) {
  if(!nrow(d)) return(data.frame())
  d <- d[!is.na(d$distance_m) & nzchar(d$trial_label),,drop=FALSE]
  if(!nrow(d)) return(data.frame())
  groups <- split(d,paste(d$node_id,d$trial_label,d$distance_m,sep="|"))
  out <- do.call(rbind,lapply(groups,function(g) {
    v <- span_delivery(g); rssi <- g$rssi_dbm[g$rssi_dbm != -127]
    data.frame(node=g$node_id[1],trial=g$trial_label[1],distance_m=g$distance_m[1],
      received=v$received,missing=v$missing,delivery_pct=round(v$pct,1),
      median_rssi=if(length(rssi)) median(rssi) else NA_real_,
      observed_s=round(as.numeric(difftime(max(g$received_at),min(g$received_at),units="secs")),1))
  }))
  out[order(out$node,out$distance_m),,drop=FALSE]
}
make_demo <- function(now=Sys.time()) {
  # All values below are synthetic; not measurements or radio range claims.
  set.seed(40)
  nodes <- c("a4cf12000101","a4cf12000102","a4cf12000103","a4cf12000104")
  output <- lapply(seq_along(nodes),function(i) {
    sequence <- 0:599; distance <- rep(c(0,15,30,45),each=150)
    keep <- runif(600)>((i-1)*0.018 + distance/1400)
    if(i==4) keep[580:600] <- FALSE
    t <- sequence[keep]; n <- length(t); relay <- i==3
    tvoc <- round(pmax(0,35+12*i+18*sin(t/45+i)+rnorm(n,0,6)+ifelse(t>350 & t<420,50,0)))
    data.frame(received_at=format(now-600+t,"%Y-%m-%dT%H:%M:%OS3Z",tz="UTC"),
      schema_version=1,node_id=nodes[i],boot_id=sprintf("%08x",i),sensor=if(relay) "none" else "sgp30",
      sensor_serial=sprintf("%012x",i),sequence=t,uptime_ms=1000*(t+20),
      eco2_ppm=if(relay) NA_real_ else round(430+tvoc*2.1+rnorm(n,0,12)),
      tvoc_ppb=if(relay) NA_real_ else tvoc,valid=!relay,status=if(relay) "relay" else "ok",
      error_code=0,humidity_compensated=FALSE,baseline_restored=FALSE,
      transport="mesh",mesh_layer=i,rssi_dbm=round(-37-distance[keep]*0.7-i*4+rnorm(n,0,3)),
      node_role=if(relay) "relay" else "sensor",trial_label=paste0("SIMULATED-step-",distance[keep]),
      distance_m=distance[keep],trial_notes="Synthetic preview only",stringsAsFactors=FALSE)
  })
  prepare_data(do.call(rbind,output))
}
