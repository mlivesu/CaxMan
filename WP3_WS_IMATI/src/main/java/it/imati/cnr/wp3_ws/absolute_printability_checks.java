/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package it.imati.cnr.wp3_ws;

import it.imati.cnr.tools.CavitiesDetection;
import it.imati.cnr.tools.GlobalChecks;
import it.imati.cnr.tools.IntegrityChecks;
import it.imati.cnr.tools.ThinwallsDetection;
import it.imati.cnr.tools.VoidDetection;
import java.io.File;
import java.io.IOException;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.jws.WebService;
import javax.jws.WebMethod;
import javax.jws.WebParam;
import javax.xml.ws.Holder;

/**
 *
 * @author cino
 */
@WebService(serviceName = "absolute_printability_checks")
public class absolute_printability_checks 
{
 
    // The namespace should match the package name in the first non-commented line of this file. 
    // If package name is a.b.c, the namespace should be "http://c.b.a/" (casae sensitive)
    // WFM will have an easier time recognizing your web service if this is fulfilled
    //
    private final String namespace = "http://wp3_ws.cnr.imati.it/";
        
    
    @WebMethod(operationName = "absolute_printability_checks_opname")
    public void absolute_printability_checks_operation (
            @WebParam(name            = "serviceID", 
                      targetNamespace = namespace, 
                      mode            = WebParam.Mode.IN)  String serviceID,
            
            @WebParam(name            = "sessionToken", 
                      targetNamespace = namespace, 
                      mode            = WebParam.Mode.IN)  String sessionToken,
            
            @WebParam(name            = "annotated_tessellation_URI_in",
                      targetNamespace = namespace,
                      mode            = WebParam.Mode.IN)  String annotated_tessellation_URI_in,
            
            @WebParam(name            = "annotated_tessellation_URI_out", 
                      targetNamespace = namespace, 
                      mode            = WebParam.Mode.OUT)  Holder<String> annotated_tessellation_URI_out,
            
            @WebParam(name            = "absolute_printability_flag", 
                      targetNamespace = namespace, 
                      mode            = WebParam.Mode.OUT)  Holder<Integer> absolute_printability_flag) 
    {
        try
        {
            DateFormat dateFormat = new SimpleDateFormat("yyyyMMdd_HHmmss");
            String sdate = dateFormat.format(new Date());
            
            String pathGSSTools         = "/root/infrastructureClients/gssClients/gssPythonClients/";
            
            String downloadedFilename       = "/root/CAxManIO/dowloaded_" + sdate + ".zip";   
            String geometricChecksFilename  = "/root/CAxManIO/geometric_checked_" + sdate + ".zip";
            String voidFilename             = "/root/CAxManIO/voids_checked_" + sdate + ".zip";
            String thinwallsFilename        = "/root/CAxManIO/thinwalls_checked_" + sdate + ".zip";
            String cavitiesFilename         = "/root/CAxManIO/cavities_checked_" + sdate + ".zip";
            String globalChecksFilename     = "/root/CAxManIO/global_checked_" + sdate + ".zip";
            String outputURI                = "swift://caxman/imati-ge/abs_checked_" + sdate + ".zip";
            
            String lastOutput   = "";
            absolute_printability_flag.value = 0;

            
            // Download File
            String cmdDownload = "python " + pathGSSTools + "download_gss.py " + annotated_tessellation_URI_in + " " + downloadedFilename + " " + sessionToken;
            System.out.print("[RUNNING] : " + cmdDownload);
            
            Process p_download = Runtime.getRuntime().exec(cmdDownload);
            
            p_download.waitFor();   // wait the download process to finish its task
            
            System.out.print("[COMPLETED] : " + cmdDownload);
            
            // Check if the input has been downloaded
            File input = new File(downloadedFilename);
            
            if (!input.getAbsoluteFile().exists()) 
                throw new IOException("Error in downloading " + annotated_tessellation_URI_in);
            
            //////////////////////////////////////////////////////////////////////////////////
            // Integrity Checks
            IntegrityChecks ic = new IntegrityChecks();

            int result_ic = ic.run(downloadedFilename, geometricChecksFilename);
            
            if (result_ic != 0)
            {
                lastOutput    = geometricChecksFilename;
                absolute_printability_flag.value        = 1;
            }
            else
            {
            
                //////////////////////////////////////////////////////////////////////////////////
                // Void Detection
                VoidDetection vd = new VoidDetection();
                int result_vd = vd.run(geometricChecksFilename, voidFilename);
                
                lastOutput = voidFilename;
               
                if (result_vd != 0)
                {
                    absolute_printability_flag.value    = 1;
                }
                else
                {
                    //////////////////////////////////////////////////////////////////////////////////
                    // ThinWalls Detection
                    ThinwallsDetection twd = new ThinwallsDetection();
                    int result_twd = twd.run (voidFilename, thinwallsFilename);

                    lastOutput = thinwallsFilename;
                    
                    if (result_twd != 0)
                    {
                        absolute_printability_flag.value    = 1;
                    }
                    else
                    {
                        //////////////////////////////////////////////////////////////////////////////////
                        // Cavities Detection
                        CavitiesDetection cd = new CavitiesDetection();
                        int result_cd = cd.run(thinwallsFilename, cavitiesFilename);
                        
                        lastOutput = cavitiesFilename;
                        
                        if (result_cd != 0)
                        {
                            absolute_printability_flag.value    = 1;
                        }
                        else
                        {
                            //////////////////////////////////////////////////////////////////////////////////
                            // Global Checks
                            GlobalChecks gc = new GlobalChecks();
                            int result_gc = gc.run(cavitiesFilename, globalChecksFilename);
                            
                            lastOutput = globalChecksFilename;
                        
                            if (result_gc != 0)
                            {
                                absolute_printability_flag.value    = 1;
                            }

                        }
                    } 
                }
            }

            
            // Upload output
            String cmdUploadOutput = "python " + pathGSSTools + "upload_gss.py " + outputURI + " " + lastOutput + " " + sessionToken;
            
            System.out.print("[RUNNING] : " + cmdUploadOutput);
            
            Process p_upload = Runtime.getRuntime().exec(cmdUploadOutput);
            
            p_upload.waitFor();   // wait the upload process to finish its task
            
            System.out.print("[COMPLETED] : " + cmdUploadOutput);
            
            // Return the address of the uploaded output
            annotated_tessellation_URI_out.value      = outputURI;

        }
        catch(IOException e)
        {           
            annotated_tessellation_URI_out.value      = "";
            absolute_printability_flag.value = 1;
            
            System.err.println("ERROR: " + e.getMessage());
        }      
        catch(InterruptedException e)
        {
            annotated_tessellation_URI_out.value      = "";
            absolute_printability_flag.value = 1;
            
            System.err.println("ERROR: " + e.getMessage());
        }
    }
    
    
    /*
    *  Utility function for less verbose logging
    */
    private void log(String message) {
        Logger.getLogger(this.getClass().getName()).log(Level.INFO, message);
    }
    
    /*
    *  Utility function for less verbose error message in log
    */
    private void error(String message) {
        Logger.getLogger(this.getClass().getName()).log(Level.SEVERE, message);
    }
    
    /*
    *  Utility function for less verbose error message in log
    */
    private void error(IOException ex) {
        Logger.getLogger(absolute_printability_checks.class.getName()).log(Level.SEVERE, null, ex);
    }}
