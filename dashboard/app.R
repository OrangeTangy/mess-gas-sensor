library(shiny)
library(jsonlite)
library(curl)
source("analytics.R", local=TRUE)

palette <- c("#000000","#555555","#888888","#aaaaaa","#333333","#777777")
css <- "
#node_table,#range_table { overflow-x:auto; }
body { background:white; color:black; font-family:Arial,sans-serif; }
.container-fluid { max-width:1400px; padding:20px; }
.hero { margin-bottom:20px; }
.hero-badge,.brand,.hint,.lede,.footer { color:#444; }
.panelx { border:1px solid #ccc; padding:15px; margin-bottom:18px; }
.metrics { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; margin:18px 0; }
.metric { border:1px solid #999; padding:15px; }
.metric .value { font-size:28px; font-weight:bold; }
.metric .labelx,.metric .note,.hint,.footer { font-size:12px; }
.mode-banner { border:2px solid black; padding:12px; margin-bottom:18px; }
a,.btn,.nav-tabs>li>a { color:black; }
.btn-primary,.btn-primary:hover { background:black; border-color:black; color:white; }
.form-control,.selectize-input { border-radius:0; box-shadow:none; }
@media(max-width:800px) { .metrics { grid-template-columns:repeat(2,1fr); } }
"
metric <- function(label, value, note) div(class="metric",div(class="labelx",label),div(class="value",value),div(class="note",note))
ui <- fluidPage(
  tags$head(tags$title("Gas Sensor Dash"),tags$style(HTML(css))),
  h1("Gas Sensor Dash"),
  uiOutput("mode_banner"),
  div(class="panelx control-panel",fluidRow(
    column(3,selectInput("mode","Data source",c("Simulated preview"="demo","Live collector"="live"))),
    column(4,textInput("endpoint","Collector address",Sys.getenv("MESS_COLLECTOR","http://127.0.0.1:8000"))),
    column(3,selectInput("nodes","Nodes",choices=c("All nodes"="all"))),
    column(2,selectInput("window","Time window",c("10 minutes"=10,"30 minutes"=30,"2 hours"=120,"All loaded"=0),selected=10))
  )),
  uiOutput("metrics"),
  tabsetPanel(id="view",
    tabPanel("Live air quality",value="air",
      fluidRow(column(8,div(class="panelx",h3("Total volatile organic compounds"),
        p(class="hint","SGP30 TVOC output · ppb · valid sensor readings only"),plotOutput("tvoc_plot",height="280px"))),
        column(4,div(class="panelx",h3("At a glance"),uiOutput("insights"),hr(),
          p(class="hint","eCO₂ is a gas-derived estimate, not a direct CO₂ measurement. This dashboard does not assign health or safety categories."),
          downloadButton("download","Export selected readings")))),
      div(class="panelx",h3("CO₂-equivalent estimate"),p(class="hint","SGP30 CO₂-equivalent output · ppm · estimated, not directly measured CO₂"),plotOutput("eco2_plot",height="235px"))),
    tabPanel("Network health",value="network",
      div(class="panelx",h3("Every node, accounted for"),p(class="hint","Offline after 10 seconds without a packet. Signal strength describes the node’s parent link; mesh layer is not a full topology map."),tableOutput("node_table")),
      fluidRow(column(7,div(class="panelx",h3("Parent link signal"),plotOutput("rssi_plot",height="280px"))),
        column(5,div(class="panelx",h3("Delivery within received span"),plotOutput("delivery_plot",height="280px")))),
      div(class="panelx",p(class="hint","Delivery estimates use unique sequence numbers within each node/boot span. They do not count losses before the first or after the last received packet. Check last-seen age for trailing outages. Link quality is not a guarantee of distance."))),
    tabPanel("Range experiments",value="range",
      fluidRow(column(4,div(class="panelx",h3("Mark a test step"),
        p(class="hint","Start a unique label at each placement. Annotations apply to new live records from the selected node."),
        selectInput("trial_node","Node to annotate",choices=NULL),
        textInput("trial_label","Unique test label","hallway-run1-step1"),
        numericInput("distance","Measured distance from gateway (m)",0,min=0,max=100000),
        textAreaInput("notes","Placement / walls / relay notes","",rows=3),
        actionButton("save_trial","Start this test step",class="btn-primary"),uiOutput("trial_result"))),
        column(8,div(class="panelx",h3("Distance and delivery"),p(class="hint","Distance is entered by your team; it is not measured by the radio."),plotOutput("range_plot",height="340px")))),
      div(class="panelx",h3("Experiment ledger"),tableOutput("range_table"),
        p(class="hint","Use fixed-duration trials and record outage time separately. A 100% result from a short received span does not establish reliable maximum range.")))),
  div(class="footer","MESS GAS TEAM · Local R Shiny dashboard · Collection and radio operation require your hardware")
)
server <- function(input,output,session) {
  source_data <- reactive({
    invalidateLater(2000,session)
    if(input$mode=="demo") return(list(data=make_demo(),error=NULL))
    endpoint <- sub("/+$","",input$endpoint)
    tryCatch({
      handle <- new_handle(timeout=3,connecttimeout=2)
      response <- curl_fetch_memory(paste0(endpoint,"/snapshot?limit=20000"),handle=handle)
      if(response$status_code!=200) stop(paste("Collector returned HTTP",response$status_code))
      payload <- fromJSON(rawToChar(response$content))
      list(data=prepare_data(payload$readings),error=NULL)
    },error=function(e) list(data=data.frame(),error=conditionMessage(e)))
  })
  observe({
    d <- source_data()$data; choices <- if(nrow(d)) sort(unique(d$node_id)) else character()
    selected <- isolate(input$nodes)
    updateSelectInput(session,"nodes",choices=c("All nodes"="all",setNames(choices,choices)),
      selected=if(length(selected)==1 && selected %in% c("all",choices)) selected else "all")
    selected_trial <- isolate(input$trial_node)
    updateSelectInput(session,"trial_node",choices=choices,
      selected=if(length(selected_trial)==1 && selected_trial %in% choices) selected_trial else head(choices,1))
  })
  filtered <- reactive({
    d <- source_data()$data
    if(!nrow(d)) return(d)
    if(!is.null(input$nodes) && input$nodes!="all") d <- d[d$node_id==input$nodes,,drop=FALSE]
    minutes <- if(is.null(input$window)) 10 else as.numeric(input$window)
    if(minutes>0) d <- d[d$received_at>=Sys.time()-minutes*60,,drop=FALSE]
    d
  })
  summaries <- reactive(node_summary(filtered()))
  output$mode_banner <- renderUI({
    s <- source_data()
    if(input$mode=="demo") div(class="mode-banner",strong("SIMULATED PREVIEW")," — all readings, distances, and link metrics are synthetic. No hardware is connected to this preview.")
    else if(!is.null(s$error)) div(class="mode-banner error",strong("COLLECTOR UNREACHABLE"),paste("—",s$error,"· No simulated data is substituted."))
    else div(class="mode-banner live",strong("LIVE COLLECTOR"),paste("—",nrow(s$data),"stored records loaded · refresh every 2 seconds · inspect node age for freshness"))
  })
  output$metrics <- renderUI({
    d <- filtered(); s <- summaries(); v <- span_delivery(d)
    valid <- if(nrow(d)) d[d$valid & d$node_role=="sensor",,drop=FALSE] else d
    latest <- if(nrow(valid)) tail(valid,1) else NULL
    div(class="metrics",
      metric("Reporting nodes",if(nrow(s)) paste0(sum(s$age_s<=10)," / ",nrow(s)) else "0 / 0","Seen within 10 seconds / known in window"),
      metric("Latest valid TVOC",if(is.null(latest)) "—" else paste(latest$tvoc_ppb,"ppb"),if(is.null(latest)) "Awaiting a valid sample" else paste("Sample age",round(as.numeric(difftime(Sys.time(),latest$received_at,units="secs"))),"s")),
      metric("Span delivery",if(is.na(v$pct)) "—" else sprintf("%.1f%%",v$pct),paste(v$missing,"missing inside received spans")),
      metric("Samples loaded",format(nrow(d),big.mark=","),"Selected nodes and time window"))
  })
  series_plot <- function(d,field,label,valid_only=TRUE) {
    par(mar=c(3,4,1,1),bg="white",fg="#333333",col.axis="#333333",col.lab="#333333")
    if(nrow(d) && valid_only) d <- d[d$valid & d$node_role=="sensor",,drop=FALSE]
    if(nrow(d) && field=="rssi_dbm") d <- d[d$rssi_dbm != -127,,drop=FALSE]
    if(!nrow(d) || all(!is.finite(d[[field]]))) { plot.new(); text(.5,.5,"Awaiting telemetry",col="#333333"); return() }
    nodes <- sort(unique(d$node_id)); values <- d[[field]]
    ylim <- range(values[is.finite(values)]); if(diff(ylim)==0) ylim <- ylim+c(-1,1)
    xlim <- range(d$received_at); if(diff(as.numeric(xlim))==0) xlim <- xlim+c(-1,1)
    plot(xlim,ylim,type="n",xlab="",ylab=label,bty="n",yaxt="n",xaxt="n")
    axis(2,las=1,tck=0); axis.POSIXct(1,at=pretty(xlim,n=5),format="%H:%M",tck=0)
    abline(h=pretty(ylim),col="#dddddd",lwd=1)
    for(i in seq_along(nodes)) {
      g <- d[d$node_id==nodes[i],,drop=FALSE]
      # Break lines at boot changes and gaps > 3 seconds rather than suggesting continuity.
      segment <- cumsum(c(TRUE,diff(as.numeric(g$received_at))>3 | head(g$boot_id,-1)!=tail(g$boot_id,-1)))
      for(part in split(g,segment)) lines(part$received_at,part[[field]],col=palette[(i-1)%%length(palette)+1],lty=(i-1)%%4+1,lwd=1.6)
    }
    legend("topleft",legend=paste0("…",substr(nodes,9,12)),col=palette[seq_along(nodes)],lty=(seq_along(nodes)-1)%%4+1,lwd=2,bty="n",horiz=TRUE,cex=.8)
  }
  output$tvoc_plot <- renderPlot(series_plot(filtered(),"tvoc_ppb","ppb"),res=110)
  output$eco2_plot <- renderPlot(series_plot(filtered(),"eco2_ppm","ppm"),res=110)
  output$rssi_plot <- renderPlot(series_plot(filtered(),"rssi_dbm","dBm",FALSE),res=110)
  output$node_table <- renderTable({
    s <- summaries(); shiny::validate(need(nrow(s)>0,"No nodes received yet.")); s
  },striped=FALSE,spacing="s",rownames=FALSE,na="—")
  output$delivery_plot <- renderPlot({
    s <- summaries(); par(mar=c(4,4,1,1),bg="white",col.axis="#333333")
    if(!nrow(s)) {plot.new();text(.5,.5,"Awaiting telemetry");return()}
    barplot(s$delivery_pct,names.arg=paste0("…",substr(s$node,9,12)),ylim=c(0,100),
      col=palette[seq_len(nrow(s))],border=NA,ylab="Received / sequence span (%)",las=1)
  },res=110)
  output$insights <- renderUI({
    d <- filtered(); s <- summaries()
    if(!nrow(d)) return(p(class="hint","Connect the collector and a node to see sensor statistics."))
    valid <- d[d$valid & d$node_role=="sensor",,drop=FALSE]
    tagList(p(strong(sum(s$age_s>10))," node(s) currently offline in this window."),
      if(nrow(valid)) p("TVOC average ",strong(sprintf("%.1f ppb",mean(valid$tvoc_ppb,na.rm=TRUE))),
                        "; range ",min(valid$tvoc_ppb,na.rm=TRUE),"–",max(valid$tvoc_ppb,na.rm=TRUE)," ppb."),
      p(sum(d$status=="sensor_error")," sensor-error records; ",sum(d$status=="warming_up")," startup records."),
      p(class="hint","Statistics exclude invalid gas values. No humidity compensation is applied unless the firmware reports a fresh external humidity input."))
  })
  output$range_table <- renderTable({
    v <- range_summary(filtered()); shiny::validate(need(nrow(v)>0,"No annotated range trials yet.")); v
  },rownames=FALSE,na="—")
  output$range_plot <- renderPlot({
    d <- range_summary(filtered()); par(mar=c(4,4,1,1),bg="white",col.axis="#333333")
    if(!nrow(d)) {plot.new();text(.5,.5,"Annotate a range test to begin");return()}
    plot(range(c(0,d$distance_m)),c(0,100),type="n",xlab="Measured distance from gateway (m)",ylab="Span delivery (%)",bty="n")
    abline(h=c(50,75,95,100),col="#dddddd",lty=2)
    nodes <- unique(d$node)
    for(i in seq_along(nodes)) {g <- d[d$node==nodes[i],];points(g$distance_m,g$delivery_pct,pch=19,col=palette[i],cex=1.3)}
    legend("bottomleft",paste0("…",substr(nodes,9,12)),col=palette[seq_along(nodes)],pch=19,bty="n")
  },res=110)
  trial_message <- reactiveVal("")
  observeEvent(input$save_trial,{
    if(input$mode!="live") {trial_message("Switch to Live collector to annotate actual experiments. Preview is read-only.");return()}
    if(is.null(input$trial_node) || !nzchar(input$trial_label)) {trial_message("Choose a node and a unique test label.");return()}
    tryCatch({
      body <- toJSON(list(node_id=input$trial_node,trial_label=input$trial_label,distance_m=input$distance,trial_notes=input$notes),auto_unbox=TRUE)
      h <- new_handle(postfields=body,timeout=3); handle_setheaders(h,"Content-Type"="application/json")
      response <- curl_fetch_memory(paste0(sub("/+$","",input$endpoint),"/trial"),handle=h)
      if(response$status_code!=200) stop(paste("HTTP",response$status_code))
      trial_message("Saved. New packets from this node will carry this test label and distance.")
    },error=function(e) trial_message(paste("Could not save:",conditionMessage(e))))
  })
  output$trial_result <- renderUI(p(class="hint",trial_message()))
  output$download <- downloadHandler(
    contentType="text/csv; charset=utf-8",
    filename=function() paste0("mess-gas-",input$mode,"-",Sys.Date(),".csv"),
    content=function(file) {
      d <- filtered(); if(nrow(d)) d$received_at <- format(d$received_at,"%Y-%m-%dT%H:%M:%OS3Z",tz="UTC")
      d$data_source <- rep(if(input$mode=="demo") "SIMULATED" else "LIVE_COLLECTOR",nrow(d))
      write.csv(d,file,row.names=FALSE,na="")
    })
}
shinyApp(ui,server)
